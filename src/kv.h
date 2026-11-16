#ifndef KV_H
#define KV_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct Entry {
    char *key;
    char *val;
    uint64_t hash;
    struct Entry *next;     // Intrusive hash table chaining
    ssize_t heap_idx;       // Index in TTL min-heap (-1 if no TTL)
    uint64_t expire_at_ms;  // Monotonic timestamp in ms
} Entry;

uint64_t now_ms(void);
void entry_free(Entry *e);

#endif