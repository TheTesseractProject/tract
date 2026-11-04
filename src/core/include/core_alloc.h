#ifndef CORE_ALLOC_H
#define CORE_ALLOC_H

#include <stddef.h>

void *core_alloc(size_t bytes);
void *core_realloc(void *ptr, size_t old_bytes, size_t new_bytes);
void core_free(void *ptr);

size_t core_heap_used(void);
size_t core_heap_capacity(void);

#endif /* CORE_ALLOC_H */