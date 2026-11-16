#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include "aof.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>

#define MAX_ARGS 8

static void write_resp(char *out_buf, size_t *out_len, uint8_t status, const char *msg) {
    uint32_t msg_len = (uint32_t)strlen(msg);
    uint32_t net_len = htonl(1 + msg_len);
    memcpy(out_buf, &net_len, 4);
    out_buf[4] = (char)status; // 0 = OK, 1 = NIL, 2 = ERR
    memcpy(out_buf + 5, msg, msg_len);
    *out_len = 5 + msg_len;
}

ssize_t handle_request(
    const char *in_buf, size_t in_len,
    char *out_buf, size_t out_cap, size_t *out_len,
    Dict *dict, Heap *heap, int aof_fd
) {
    (void)out_cap;
    if (in_len < 4) return 0;

    uint32_t payload_len;
    memcpy(&payload_len, in_buf, 4);
    payload_len = ntohl(payload_len);

    if (payload_len > 1024 * 1024) return -1; // Guard against oversized frames
    if (in_len < 4 + payload_len) return 0;   // Incomplete frame; wait for more bytes

    const char *p = in_buf + 4;
    const char *end = p + payload_len;

    if (p + 4 > end) return -1;
    uint32_t nargs;
    memcpy(&nargs, p, 4);
    nargs = ntohl(nargs);
    p += 4;

    if (nargs == 0 || nargs > MAX_ARGS) return -1;

    char *args[MAX_ARGS] = {0};
    for (uint32_t i = 0; i < nargs; i++) {
        if (p + 4 > end) goto parse_err;
        uint32_t alen;
        memcpy(&alen, p, 4);
        alen = ntohl(alen);
        p += 4;
        if (p + alen > end) goto parse_err;

        args[i] = malloc(alen + 1);
        memcpy(args[i], p, alen);
        args[i][alen] = '\0';
        p += alen;
    }

    int mutate = 0;

    if (strcasecmp(args[0], "SET") == 0 && nargs == 3) {
        dict_set(dict, args[1], args[2], heap);
        write_resp(out_buf, out_len, 0, "OK");
        mutate = 1;
    } else if (strcasecmp(args[0], "GET") == 0 && nargs == 2) {
        Entry *e = dict_get(dict, args[1]);
        if (!e) {
            write_resp(out_buf, out_len, 1, "(nil)");
        } else if (e->heap_idx != -1 && e->expire_at_ms <= now_ms()) {
            dict_del(dict, args[1], heap);
            write_resp(out_buf, out_len, 1, "(nil)");
        } else {
            write_resp(out_buf, out_len, 0, e->val);
        }
    } else if (strcasecmp(args[0], "DEL") == 0 && nargs == 2) {
        int deleted = dict_del(dict, args[1], heap);
        write_resp(out_buf, out_len, 0, deleted ? "1" : "0");
        if (deleted) mutate = 1;
    } else if (strcasecmp(args[0], "EXPIRE") == 0 && nargs == 3) {
        Entry *e = dict_get(dict, args[1]);
        long ttl_sec = atol(args[2]);
        if (!e || ttl_sec <= 0) {
            write_resp(out_buf, out_len, 0, "0");
        } else {
            heap_upsert(heap, e, now_ms() + (uint64_t)ttl_sec * 1000ULL);
            write_resp(out_buf, out_len, 0, "1");
            mutate = 1;
        }
    } else {
        write_resp(out_buf, out_len, 2, "ERR unknown command or wrong arity");
    }

    if (mutate && aof_fd >= 0) {
        aof_append(aof_fd, in_buf, 4 + payload_len);
    }

    for (uint32_t i = 0; i < nargs; i++) free(args[i]);
    return (ssize_t)(4 + payload_len);

parse_err:
    for (uint32_t i = 0; i < nargs; i++) free(args[i]);
    return -1;
}