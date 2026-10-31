#ifndef CORE_ALLOC_H
#define CORE_ALLOC_H

#include <stddef.h>

void *core_alloc(size_t bytes);
void *core_realloc(void *ptr, size_t old_bytes, size_t new_bytes);
void core_free(void *ptr);

size_t core_heap_used(void);
size_t core_heap_capacity(void);

/* Compatibility aliases for the current pico codebase. */
#define pico_alloc core_alloc
#define pico_realloc core_realloc
#define pico_free core_free
#define pico_heap_used core_heap_used
#define pico_heap_capacity core_heap_capacity

#endif /* CORE_ALLOC_H */