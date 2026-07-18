#include "verve/vv_arena.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct vv_ArenaBlock {
    vv_ArenaBlock *next;
    size_t         cap;   // usable bytes in `data`
    size_t         used;  // bytes handed out from this block
    // data follows the header in the same allocation
    _Alignas(_Alignof(max_align_t)) unsigned char data[];
};

static vv_ArenaBlock *block_create(size_t cap) {
    vv_ArenaBlock *b = malloc(sizeof(vv_ArenaBlock) + cap);
    assert(b && "vv_arena: out of memory");
    b->next = NULL;
    b->cap  = cap;
    b->used = 0;
    return b;
}

void vv_arena_init(vv_Arena *a, size_t block_size) {
    if (block_size < 4096) block_size = 4096;
    a->block_size     = block_size;
    a->first          = block_create(block_size);
    a->head           = a->first;
    a->total_reserved = block_size;
    a->total_used     = 0;
}

void vv_arena_destroy(vv_Arena *a) {
    vv_ArenaBlock *b = a->first;
    while (b) {
        vv_ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    memset(a, 0, sizeof(*a));
}

static size_t align_up(size_t n, size_t align) {
    return (n + (align - 1)) & ~(align - 1);
}

void *vv_arena_alloc_aligned(vv_Arena *a, size_t size, size_t align) {
    assert((align & (align - 1)) == 0 && "align must be power of two");

    for (;;) {
        vv_ArenaBlock *b = a->head;
        size_t base   = (size_t)b->data;
        size_t cursor = base + b->used;
        size_t aligned = align_up(cursor, align);
        size_t pad     = aligned - cursor;

        if (b->used + pad + size <= b->cap) {
            b->used += pad + size;
            a->total_used += pad + size;
            return (void *)aligned;
        }

        // Grow: reuse a chained block if one already exists (post-reset),
        // otherwise allocate a fresh one sized to fit even oversized requests.
        if (b->next && size + align <= b->next->cap) {
            a->head = b->next;
            continue;
        }
        size_t need = size + align;
        size_t cap  = need > a->block_size ? need : a->block_size;
        vv_ArenaBlock *nb = block_create(cap);
        nb->next = b->next;
        b->next  = nb;
        a->head  = nb;
        a->total_reserved += cap;
    }
}

void *vv_arena_alloc(vv_Arena *a, size_t size) {
    return vv_arena_alloc_aligned(a, size, _Alignof(max_align_t));
}

void *vv_arena_calloc(vv_Arena *a, size_t size) {
    void *p = vv_arena_alloc(a, size);
    memset(p, 0, size);
    return p;
}

void vv_arena_reset(vv_Arena *a) {
    for (vv_ArenaBlock *b = a->first; b; b = b->next) b->used = 0;
    a->head       = a->first;
    a->total_used = 0;
}
