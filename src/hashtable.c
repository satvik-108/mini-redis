#define _POSIX_C_SOURCE 200809L
#include "hashtable.h"
#include "heap.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_SIZE 16
#define REHASH_BATCH 64

// FNV-1a 64-bit hash
static uint64_t hash_key(const char *key) {
    uint64_t h = 14695981039346656037ULL;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return h;
}

static void htable_init(HTable *ht, size_t size) {
    ht->buckets = calloc(size, sizeof(Entry *));
    ht->size = size;
    ht->mask = size - 1;
    ht->used = 0;
}

void dict_init(Dict *d) {
    htable_init(&d->ht[0], INITIAL_SIZE);
    memset(&d->ht[1], 0, sizeof(HTable));
    d->rehash_idx = -1;
}

static void dict_start_rehash(Dict *d) {
    if (d->rehash_idx != -1) return;
    htable_init(&d->ht[1], d->ht[0].size * 2);
    d->rehash_idx = 0;
}

void dict_rehash_step(Dict *d, int max_steps) {
    if (d->rehash_idx == -1) return;

    int steps = 0;
    while (steps < max_steps && d->ht[0].used > 0) {
        while ((size_t)d->rehash_idx < d->ht[0].size && d->ht[0].buckets[d->rehash_idx] == NULL) {
            d->rehash_idx++;
        }
        if ((size_t)d->rehash_idx >= d->ht[0].size) break;

        Entry *curr = d->ht[0].buckets[d->rehash_idx];
        d->ht[0].buckets[d->rehash_idx] = NULL;

        while (curr) {
            Entry *next = curr->next;
            size_t idx = curr->hash & d->ht[1].mask;
            curr->next = d->ht[1].buckets[idx];
            d->ht[1].buckets[idx] = curr;
            d->ht[0].used--;
            d->ht[1].used++;
            curr = next;
        }
        d->rehash_idx++;
        steps++;
    }

    if (d->ht[0].used == 0) {
        free(d->ht[0].buckets);
        d->ht[0] = d->ht[1];
        memset(&d->ht[1], 0, sizeof(HTable));
        d->rehash_idx = -1;
    }
}

Entry *dict_get(Dict *d, const char *key) {
    dict_rehash_step(d, REHASH_BATCH);
    uint64_t h = hash_key(key);

    for (int table = 0; table <= 1; table++) {
        if (table == 1 && d->rehash_idx == -1) break;
        size_t idx = h & d->ht[table].mask;
        for (Entry *e = d->ht[table].buckets[idx]; e != NULL; e = e->next) {
            if (e->hash == h && strcmp(e->key, key) == 0) {
                return e;
            }
        }
    }
    return NULL;
}

void dict_set(Dict *d, const char *key, const char *val, Heap *heap) {
    dict_rehash_step(d, REHASH_BATCH);
    Entry *existing = dict_get(d, key);
    if (existing) {
        free(existing->val);
        existing->val = strdup(val);
        if (existing->heap_idx != -1) {
            heap_remove(heap, existing->heap_idx);
        }
        return;
    }

    if (d->rehash_idx == -1 && d->ht[0].used >= (d->ht[0].size * 3) / 4) {
        dict_start_rehash(d);
    }

    HTable *target = (d->rehash_idx != -1) ? &d->ht[1] : &d->ht[0];
    uint64_t h = hash_key(key);
    size_t idx = h & target->mask;

    Entry *e = malloc(sizeof(Entry));
    e->key = strdup(key);
    e->val = strdup(val);
    e->hash = h;
    e->heap_idx = -1;
    e->expire_at_ms = 0;
    e->next = target->buckets[idx];
    target->buckets[idx] = e;
    target->used++;
}

int dict_del(Dict *d, const char *key, Heap *heap) {
    dict_rehash_step(d, REHASH_BATCH);
    uint64_t h = hash_key(key);

    for (int table = 0; table <= 1; table++) {
        if (table == 1 && d->rehash_idx == -1) break;
        size_t idx = h & d->ht[table].mask;
        Entry **prev = &d->ht[table].buckets[idx];
        while (*prev) {
            Entry *curr = *prev;
            if (curr->hash == h && strcmp(curr->key, key) == 0) {
                *prev = curr->next;
                d->ht[table].used--;
                if (curr->heap_idx != -1) {
                    heap_remove(heap, curr->heap_idx);
                }
                entry_free(curr);
                return 1;
            }
            prev = &curr->next;
        }
    }
    return 0;
}

void dict_free(Dict *d) {
    for (int table = 0; table <= 1; table++) {
        if (!d->ht[table].buckets) continue;
        for (size_t i = 0; i < d->ht[table].size; i++) {
            Entry *curr = d->ht[table].buckets[i];
            while (curr) {
                Entry *next = curr->next;
                entry_free(curr);
                curr = next;
            }
        }
        free(d->ht[table].buckets);
    }
}