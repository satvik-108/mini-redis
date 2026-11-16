#define _POSIX_C_SOURCE 200809L
#include "kv.h"
#include "hashtable.h"
#include "heap.h"
#include "aof.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>

#define PORT 6379
#define MAX_EVENTS 128
#define BUF_SIZE 16384

typedef struct {
    int fd;
    char read_buf[BUF_SIZE];
    size_t read_len;
    char write_buf[BUF_SIZE];
    size_t write_len;
    size_t write_sent;
} Client;

static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void entry_free(Entry *e) {
    if (!e) return;
    free(e->key);
    free(e->val);
    free(e);
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void client_close(int epfd, Client *c) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    free(c);
}

static void update_epoll_events(int epfd, Client *c) {
    struct epoll_event ev = {0};
    ev.data.ptr = c;
    ev.events = EPOLLIN | EPOLLRDHUP;
    if (c->write_sent < c->write_len) {
        ev.events |= EPOLLOUT;
    }
    epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

static void expire_due_keys(Dict *dict, Heap *heap) {
    uint64_t now = now_ms();
    int expired_count = 0;
    while (expired_count < 100) {
        Entry *top = heap_peek(heap);
        if (!top || top->expire_at_ms > now) break;
        dict_del(dict, top->key, heap);
        expired_count++;
    }
}

static int next_timeout_ms(Heap *heap) {
    Entry *top = heap_peek(heap);
    if (!top) return 100; // Default 100ms tick for progressive rehashing
    uint64_t now = now_ms();
    if (top->expire_at_ms <= now) return 0;
    uint64_t diff = top->expire_at_ms - now;
    return (diff > 100) ? 100 : (int)diff;
}

int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    Dict dict;
    Heap heap;
    dict_init(&dict);
    heap_init(&heap);

    printf("Replaying AOF if present...\n");
    aof_replay("appendonly.aof", &dict, &heap);
    int aof_fd = aof_open("appendonly.aof");

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(listen_fd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    listen(listen_fd, SOMAXCONN);

    int epfd = epoll_create1(0);
    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    struct epoll_event events[MAX_EVENTS];
    printf("Mini-Redis listening on port %d...\n", PORT);

    while (running) {
        int timeout = next_timeout_ms(&heap);
        int n = epoll_wait(epfd, events, MAX_EVENTS, timeout);

        expire_due_keys(&dict, &heap);
        dict_rehash_step(&dict, 16);

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                while (1) {
                    int cfd = accept(listen_fd, NULL, NULL);
                    if (cfd < 0) break;
                    set_nonblocking(cfd);
                    Client *c = calloc(1, sizeof(Client));
                    c->fd = cfd;
                    struct epoll_event cev = {0};
                    cev.events = EPOLLIN | EPOLLRDHUP;
                    cev.data.ptr = c;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
                }
            } else {
                Client *c = (Client *)events[i].data.ptr;
                uint32_t evs = events[i].events;

                if (evs & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    client_close(epfd, c);
                    continue;
                }

                if (evs & EPOLLIN) {
                    ssize_t r = read(c->fd, c->read_buf + c->read_len, BUF_SIZE - c->read_len);
                    if (r <= 0) {
                        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            // Try again next tick
                        } else {
                            client_close(epfd, c);
                            continue;
                        }
                    } else {
                        c->read_len += (size_t)r;
                        while (c->read_len > 0) {
                            size_t resp_len = 0;
                            ssize_t consumed = handle_request(
                                c->read_buf, c->read_len,
                                c->write_buf + c->write_len, BUF_SIZE - c->write_len,
                                &resp_len, &dict, &heap, aof_fd
                            );
                            if (consumed < 0) {
                                client_close(epfd, c);
                                c = NULL;
                                break;
                            }
                            if (consumed == 0) break;

                            c->write_len += resp_len;
                            size_t rem = c->read_len - (size_t)consumed;
                            if (rem > 0) {
                                memmove(c->read_buf, c->read_buf + consumed, rem);
                            }
                            c->read_len = rem;
                        }
                        if (!c) continue;
                    }
                }

                if (evs & EPOLLOUT) {
                    ssize_t w = write(c->fd, c->write_buf + c->write_sent, c->write_len - c->write_sent);
                    if (w > 0) {
                        c->write_sent += (size_t)w;
                        if (c->write_sent == c->write_len) {
                            c->write_sent = 0;
                            c->write_len = 0;
                        }
                    }
                }

                if (c) update_epoll_events(epfd, c);
            }
        }
    }

    printf("\nShutting down cleanly...\n");
    close(listen_fd);
    close(epfd);
    if (aof_fd >= 0) close(aof_fd);
    dict_free(&dict);
    heap_free(&heap);
    return 0;
}