#include "core_alloc.h"
#include "core_profile.h"

#include <stdint.h>
#include <string.h>

#if defined(CORE_NO_HEAP)

/*
 * Embedded profile: one static, 16-byte aligned arena, bump-allocated, with a
 * small reuse cache.
 *
 * A pure bump allocator cannot serve a long-lived process: core_free has
 * nowhere to return a block, so a VM that loads a container per request
 * exhausts the arena and every later allocation fails while only a few
 * kilobytes are live. Each block therefore carries a header, and a freed block
 * is either popped from the top of the arena or parked in the cache for the
 * next allocation of the same shape. Still zero syscalls, zero malloc, and the
 * resident set stays bounded by the arena: a cache entry that has to be evicted
 * is leaked, never aliased.
 */
#define CORE_BLOCK_LIVE 0x50494346u /* "PICF" while the block belongs to the caller */
#define CORE_BLOCK_SPARE 0x53505244u /* "SPRD" while it sits in the cache     */
#define CORE_FREE_CACHE 16u

typedef struct {
    uint32_t magic;
    uint32_t bytes;
    uint64_t pad;
} core_block_header;

_Static_assert(sizeof(core_block_header) == 16, "block header must be 16 bytes");

static unsigned char g_arena[CORE_EMBEDDED_HEAP_BYTES] __attribute__((aligned(16)));
static size_t g_used;

static struct {
    unsigned char *at;
    size_t bytes;
} g_cache[CORE_FREE_CACHE];

static size_t align_up(size_t n) {
    return (n + 15u) & ~(size_t)15u;
}

/* Usable size of a block, given the bytes the caller asked for. */
static size_t block_bytes(size_t request) {
    return align_up(request == 0 ? 1 : request);
}

static core_block_header *header_of(void *ptr) {
    return (core_block_header *)((unsigned char *)ptr - sizeof(core_block_header));
}

static void cache_drop(size_t slot) {
    g_cache[slot].at = NULL;
    g_cache[slot].bytes = 0;
}

/* Best fit: the smallest parked block that still serves the request, so a mix
 * of table sizes does not fragment the cache down to its smallest entry. */
static void *cache_take(size_t bytes) {
    size_t best = CORE_FREE_CACHE;
    for (size_t i = 0; i < CORE_FREE_CACHE; ++i) {
        if (g_cache[i].at != NULL && g_cache[i].bytes >= bytes &&
            (best == CORE_FREE_CACHE || g_cache[i].bytes < g_cache[best].bytes)) {
            best = i;
        }
    }
    if (best == CORE_FREE_CACHE) {
        return NULL;
    }
    unsigned char *at = g_cache[best].at;
    cache_drop(best);
    core_block_header *h = header_of(at);
    h->magic = CORE_BLOCK_LIVE;
    /* h->bytes stays as parked: the block is at least as large as this request,
     * and recording the smaller size would strand the difference for good. */
    return at;
}

static void cache_put(unsigned char *at, size_t bytes) {
    size_t empty = CORE_FREE_CACHE;
    size_t tightest = CORE_FREE_CACHE;
    for (size_t i = 0; i < CORE_FREE_CACHE; ++i) {
        if (g_cache[i].at == NULL) {
            empty = i;
            break;
        }
        if (tightest == CORE_FREE_CACHE || g_cache[i].bytes < g_cache[tightest].bytes) {
            tightest = i;
        }
    }
    if (empty == CORE_FREE_CACHE) {
        /* Cache full. Park the larger block and lose the smaller one: a parked
         * block can serve every request up to its size, so keeping the big ones
         * reclaims more per allocation. The lost block stays in the arena,
         * unreachable but never handed out. */
        if (bytes <= g_cache[tightest].bytes) {
            return;
        }
        empty = tightest;
    }
    g_cache[empty].at = at;
    g_cache[empty].bytes = bytes;
    header_of(at)->magic = CORE_BLOCK_SPARE;
}

void *core_alloc(size_t bytes) {
    /* Checked before rounding: align_up of a near-SIZE_MAX request would wrap
     * and hand back a block far too small for the caller. */
    if (bytes > UINT32_MAX) {
        return NULL;
    }
    size_t need = block_bytes(bytes);
    void *from_cache = cache_take(need);
    if (from_cache != NULL) {
        return from_cache;
    }
    size_t total = align_up(sizeof(core_block_header) + need);
    if (total > sizeof(g_arena) || g_used > sizeof(g_arena) - total) {
        return NULL;
    }
    unsigned char *at = &g_arena[g_used];
    core_block_header *h = (core_block_header *)at;
    h->magic = CORE_BLOCK_LIVE;
    h->bytes = (uint32_t)need;
    h->pad = 0;
    g_used += total;
    return at + sizeof(core_block_header);
}

void *core_realloc(void *ptr, size_t old_bytes, size_t new_bytes) {
    void *p = core_alloc(new_bytes);
    if (p == NULL) {
        return NULL;
    }
    if (ptr != NULL) {
        size_t copy = old_bytes < new_bytes ? old_bytes : new_bytes;
        memcpy(p, ptr, copy);
        core_free(ptr);
    }
    return p;
}

void core_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    core_block_header *h = header_of(ptr);
    if (h->magic != CORE_BLOCK_LIVE) {
        return; /* already parked or never ours: a double free must not create two owners */
    }
    h->magic = 0;
    unsigned char *at = (unsigned char *)h;
    size_t total = align_up(sizeof(*h) + (size_t)h->bytes);
    if ((size_t)(at - g_arena) + total == g_used) {
        g_used -= total; /* top of the arena: hand the bump pointer back */
        return;
    }
    cache_put(at + sizeof(*h), (size_t)h->bytes);
}

size_t core_heap_used(void) {
    return g_used;
}

size_t core_heap_capacity(void) {
    return sizeof(g_arena);
}

#else /* host profiles */

#include <stdlib.h>

void *core_alloc(size_t bytes) {
    return malloc(bytes == 0 ? 1 : bytes);
}

void *core_realloc(void *ptr, size_t old_bytes, size_t new_bytes) {
    (void)old_bytes;
    return realloc(ptr, new_bytes == 0 ? 1 : new_bytes);
}

void core_free(void *ptr) {
    free(ptr);
}

size_t core_heap_used(void) {
    return 0;
}

size_t core_heap_capacity(void) {
    return 0;
}

#endif
