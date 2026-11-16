#ifndef HEAP_H
#define HEAP_H

#include "kv.h"

typedef struct Heap {
    Entry **items;
    size_t len;
    size_t cap;
} Heap;

void heap_init(Heap *h);
void heap_upsert(Heap *h, Entry *e, uint64_t expire_at_ms);
void heap_remove(Heap *h, size_t idx);
Entry *heap_peek(Heap *h);
void heap_free(Heap *h);

#endif