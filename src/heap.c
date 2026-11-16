#include "heap.h"
#include <stdlib.h>

void heap_init(Heap *h) {
    h->cap = 64;
    h->len = 0;
    h->items = malloc(sizeof(Entry *) * h->cap);
}

static void heap_swap(Heap *h, size_t i, size_t j) {
    Entry *tmp = h->items[i];
    h->items[i] = h->items[j];
    h->items[j] = tmp;
    h->items[i]->heap_idx = (ssize_t)i;
    h->items[j]->heap_idx = (ssize_t)j;
}

static void sift_up(Heap *h, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (h->items[idx]->expire_at_ms < h->items[parent]->expire_at_ms) {
            heap_swap(h, idx, parent);
            idx = parent;
        } else {
            break;
        }
    }
}

static void sift_down(Heap *h, size_t idx) {
    while (1) {
        size_t smallest = idx;
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;

        if (left < h->len && h->items[left]->expire_at_ms < h->items[smallest]->expire_at_ms) {
            smallest = left;
        }
        if (right < h->len && h->items[right]->expire_at_ms < h->items[smallest]->expire_at_ms) {
            smallest = right;
        }
        if (smallest != idx) {
            heap_swap(h, idx, smallest);
            idx = smallest;
        } else {
            break;
        }
    }
}

void heap_upsert(Heap *h, Entry *e, uint64_t expire_at_ms) {
    e->expire_at_ms = expire_at_ms;
    if (e->heap_idx != -1) {
        size_t idx = (size_t)e->heap_idx;
        sift_up(h, idx);
        sift_down(h, (size_t)e->heap_idx);
        return;
    }

    if (h->len == h->cap) {
        h->cap *= 2;
        h->items = realloc(h->items, sizeof(Entry *) * h->cap);
    }
    size_t idx = h->len++;
    h->items[idx] = e;
    e->heap_idx = (ssize_t)idx;
    sift_up(h, idx);
}

void heap_remove(Heap *h, size_t idx) {
    if (idx >= h->len) return;
    Entry *removed = h->items[idx];
    removed->heap_idx = -1;

    h->len--;
    if (idx < h->len) {
        h->items[idx] = h->items[h->len];
        h->items[idx]->heap_idx = (ssize_t)idx;
        sift_up(h, idx);
        sift_down(h, (size_t)h->items[idx]->heap_idx);
    }
}

Entry *heap_peek(Heap *h) {
    return (h->len > 0) ? h->items[0] : NULL;
}

void heap_free(Heap *h) {
    free(h->items);
}