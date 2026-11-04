#include "ast.h"

#include "core_alloc.h"

#include <stdalign.h>
#include <string.h>

struct tess_arena_block {
    tess_arena_block *next;
    size_t used;
    size_t cap;
    unsigned char data[];
};

#define TESS_ARENA_BLOCK_BYTES 4096u

static size_t align_up(size_t bytes) {
    size_t alignment = alignof(max_align_t);
    if (bytes > SIZE_MAX - (alignment - 1)) return 0;
    return (bytes + alignment - 1) & ~(alignment - 1);
}

void *tess_arena_alloc(tess_arena *arena, size_t bytes) {
    if (arena == NULL) {
        return NULL;
    }
    if (bytes == 0) {
        bytes = 1;
    }
    bytes = align_up(bytes);
    if (bytes == 0) return NULL;

    tess_arena_block *block = arena->head;
    if (block == NULL || block->cap - block->used < bytes) {
        size_t cap = bytes > TESS_ARENA_BLOCK_BYTES ? bytes
                                                    : TESS_ARENA_BLOCK_BYTES;
        if (cap > SIZE_MAX - sizeof(tess_arena_block)) {
            return NULL;
        }
        block = core_alloc(sizeof(tess_arena_block) + cap);
        if (block == NULL) {
            return NULL;
        }
        block->next = arena->head;
        block->used = 0;
        block->cap = cap;
        arena->head = block;
    }

    void *at = block->data + block->used;
    block->used += bytes;
    memset(at, 0, bytes);
    return at;
}

char *tess_arena_strndup(tess_arena *arena, const char *text, size_t len) {
    char *copy = tess_arena_alloc(arena, len + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (len != 0) {
        memcpy(copy, text, len);
    }
    copy[len] = '\0';
    return copy;
}

void tess_arena_destroy(tess_arena *arena) {
    if (arena == NULL) {
        return;
    }
    tess_arena_block *block = arena->head;
    while (block != NULL) {
        tess_arena_block *next = block->next;
        core_free(block);
        block = next;
    }
    arena->head = NULL;
}

void tess_program_free(tess_program *program) {
    if (program == NULL) {
        return;
    }
    tess_arena_destroy(&program->arena);
    core_free(program);
}
