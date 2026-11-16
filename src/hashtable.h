#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "kv.h"

struct Heap;

typedef struct {
    Entry **buckets;
    size_t size;
    size_t mask;
    size_t used;
} HTable;

typedef struct {
    HTable ht[2];
    ssize_t rehash_idx; // -1 when not rehashing
} Dict;

void dict_init(Dict *d);
Entry *dict_get(Dict *d, const char *key);
void dict_set(Dict *d, const char *key, const char *val, struct Heap *heap);
int dict_del(Dict *d, const char *key, struct Heap *heap);
void dict_rehash_step(Dict *d, int max_steps);
void dict_free(Dict *d);

#endif