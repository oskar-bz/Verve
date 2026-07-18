// vv_arena.h — the only allocation primitive in the core (§13).
//
// Two usage patterns, one implementation:
//   * Persistent arena: grows via chained blocks, never reset mid-session.
//   * Frame arena: reset wholesale each frame (vv_arena_reset), memory reused.
//
// Rule: no malloc outside this file's .c. Everything else takes offsets/indices
// or arena-owned pointers whose lifetime is the arena's.
#ifndef VV_ARENA_H
#define VV_ARENA_H

#include "vv_types.h"

typedef struct vv_ArenaBlock vv_ArenaBlock;

typedef struct vv_Arena {
    vv_ArenaBlock *head;      // current block we bump-allocate from
    vv_ArenaBlock *first;     // first block (kept across resets)
    size_t         block_size; // default size for freshly grown blocks
    size_t         total_reserved;
    size_t         total_used;
} vv_Arena;

// Initialize with a starting block of at least `block_size` bytes.
void  vv_arena_init(vv_Arena *a, size_t block_size);

// Release all blocks back to the OS. The arena is unusable until re-init.
void  vv_arena_destroy(vv_Arena *a);

// Allocate `size` bytes aligned to `align` (power of two). Never returns NULL
// unless the OS is out of memory (asserts in debug).
void *vv_arena_alloc_aligned(vv_Arena *a, size_t size, size_t align);

// Allocate `size` bytes with max_align_t alignment.
void *vv_arena_alloc(vv_Arena *a, size_t size);

// Allocate zero-initialized memory.
void *vv_arena_calloc(vv_Arena *a, size_t size);

// Rewind to empty, keeping blocks for reuse (frame-arena pattern).
void  vv_arena_reset(vv_Arena *a);

#define VV_ARENA_NEW(a, T)        ((T *)vv_arena_calloc((a), sizeof(T)))
#define VV_ARENA_NEW_N(a, T, n)   ((T *)vv_arena_calloc((a), sizeof(T) * (size_t)(n)))

#endif // VV_ARENA_H
