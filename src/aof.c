#define _POSIX_C_SOURCE 200809L
#include "aof.h"
#include "protocol.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int aof_open(const char *path) {
    return open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

void aof_append(int fd, const char *frame, size_t len) {
    if (fd < 0) return;
    ssize_t written = write(fd, frame, len);
    (void)written;
    fsync(fd); // Strong durability; can be batched every 1s for higher throughput
}

void aof_replay(const char *path, Dict *dict, Heap *heap) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return; // No AOF yet

    char buf[8192];
    size_t buf_len = 0;
    char out_dummy[1024];
    size_t out_len = 0;

    while (1) {
        ssize_t n = read(fd, buf + buf_len, sizeof(buf) - buf_len);
        if (n <= 0) break;
        buf_len += (size_t)n;

        while (1) {
            out_len = 0;
            // Pass aof_fd = -1 during replay so we don't re-log replayed commands
            ssize_t consumed = handle_request(buf, buf_len, out_dummy, sizeof(out_dummy), &out_len, dict, heap, -1);
            if (consumed <= 0) break;
            size_t rem = buf_len - (size_t)consumed;
            if (rem > 0) {
                for (size_t i = 0; i < rem; i++) buf[i] = buf[consumed + i];
            }
            buf_len = rem;
        }
    }
    close(fd);
}