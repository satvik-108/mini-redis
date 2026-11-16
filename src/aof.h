#ifndef AOF_H
#define AOF_H

#include "hashtable.h"
#include "heap.h"

int aof_open(const char *path);
void aof_append(int fd, const char *frame, size_t len);
void aof_replay(const char *path, Dict *dict, Heap *heap);

#endif