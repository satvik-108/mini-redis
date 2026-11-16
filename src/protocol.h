#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "hashtable.h"
#include "heap.h"
#include <stddef.h>
#include <sys/types.h>

// Returns number of bytes consumed from in_buf (0 if incomplete frame, -1 on protocol error)
ssize_t handle_request(
    const char *in_buf, size_t in_len,
    char *out_buf, size_t out_cap, size_t *out_len,
    Dict *dict, Heap *heap, int aof_fd
);

#endif