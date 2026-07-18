#include "verve/vv_node.h"

#include <assert.h>
#include <string.h>

// The pool and its ID map are arena-backed. Growth allocates a fresh, larger
// array from the persistent arena and copies over — the old block is abandoned
// (bounded: doubling makes total waste < 2x the final size). Indices are
// preserved across growth, satisfying the "indices, not pointers" rule (§13).

static void map_rehash(vv_NodePool *p, uint32_t new_cap);

void vv_pool_init(vv_NodePool *p, vv_Arena *persistent, uint32_t initial_cap) {
    if (initial_cap < 16) initial_cap = 16;
    p->persistent = persistent;
    p->nodes      = VV_ARENA_NEW_N(persistent, vv_Node, initial_cap);
    p->count      = 0;
    p->cap        = initial_cap;
    p->free_head  = VV_NIL;

    uint32_t mcap = 32;
    while (mcap < initial_cap * 2) mcap <<= 1;
    p->map     = VV_ARENA_NEW_N(persistent, vv_MapSlot, mcap);
    p->map_cap = mcap;
    p->map_len = 0;
}

static void pool_grow(vv_NodePool *p) {
    uint32_t new_cap = p->cap * 2;
    vv_Node *n = VV_ARENA_NEW_N(p->persistent, vv_Node, new_cap);
    memcpy(n, p->nodes, sizeof(vv_Node) * p->count);
    p->nodes = n;
    p->cap   = new_cap;
}

// ---- open-addressing map (linear probing) --------------------------------

static uint32_t map_probe(vv_MapSlot *slots, uint32_t cap, vv_ID id) {
    uint32_t mask = cap - 1;
    uint32_t i    = (uint32_t)(id ^ (id >> 32)) & mask;
    while (slots[i].id != 0 && slots[i].id != id) i = (i + 1) & mask;
    return i;
}

static void map_put(vv_NodePool *p, vv_ID id, uint32_t index) {
    if ((p->map_len + 1) * 10 >= p->map_cap * 7) map_rehash(p, p->map_cap * 2);
    uint32_t i = map_probe(p->map, p->map_cap, id);
    if (p->map[i].id == 0) p->map_len++;
    p->map[i].id    = id;
    p->map[i].index = index;
}

static void map_rehash(vv_NodePool *p, uint32_t new_cap) {
    vv_MapSlot *old = p->map;
    uint32_t    oldc = p->map_cap;
    p->map     = VV_ARENA_NEW_N(p->persistent, vv_MapSlot, new_cap);
    p->map_cap = new_cap;
    p->map_len = 0;
    for (uint32_t i = 0; i < oldc; i++) {
        if (old[i].id != 0) map_put(p, old[i].id, old[i].index);
    }
}

static void map_remove(vv_NodePool *p, vv_ID id) {
    uint32_t mask = p->map_cap - 1;
    uint32_t i    = map_probe(p->map, p->map_cap, id);
    if (p->map[i].id != id) return;
    // Backward-shift deletion to keep probe chains intact.
    p->map[i].id = 0;
    p->map_len--;
    uint32_t j = i;
    for (;;) {
        j = (j + 1) & mask;
        if (p->map[j].id == 0) break;
        uint32_t home = (uint32_t)(p->map[j].id ^ (p->map[j].id >> 32)) & mask;
        // Can this entry move back to fill slot i?
        bool movable = (i <= j) ? (home <= i || home > j)
                                : (home <= i && home > j);
        if (movable) {
            p->map[i] = p->map[j];
            p->map[j].id = 0;
            i = j;
        }
    }
}

// ---- pool ----------------------------------------------------------------

uint32_t vv_pool_find(vv_NodePool *p, vv_ID id) {
    uint32_t i = map_probe(p->map, p->map_cap, id);
    return p->map[i].id == id ? p->map[i].index : VV_NIL;
}

uint32_t vv_pool_obtain(vv_NodePool *p, vv_ID id, bool *created) {
    uint32_t existing = vv_pool_find(p, id);
    if (existing != VV_NIL) {
        if (created) *created = false;
        return existing;
    }

    uint32_t index;
    if (p->free_head != VV_NIL) {
        index        = p->free_head;
        p->free_head = p->nodes[index].parent; // freelist threads through `parent`
    } else {
        if (p->count == p->cap) pool_grow(p);
        index = p->count++;
    }

    vv_Node *n = &p->nodes[index];
    memset(n, 0, sizeof(*n));
    n->id          = id;
    n->parent      = VV_NIL;
    n->first_child = n->last_child = n->next_sibling = VV_NIL;
    n->flags       = VV_FLAG_ALIVE;

    map_put(p, id, index);
    if (created) *created = true;
    return index;
}

// ---- widget-state allocator ----------------------------------------------

static uint32_t ws_round(uint32_t size) { return (size + 15u) & ~15u; }

static vv_WSBucket *ws_bucket(vv_NodePool *p, uint32_t size) {
    for (int i = 0; i < p->ws_count; i++)
        if (p->ws[i].size == size) return &p->ws[i];
    if (p->ws_count < VV_WS_BUCKETS) {
        vv_WSBucket *b = &p->ws[p->ws_count++];
        b->size = size; b->head = NULL;
        return b;
    }
    return NULL; // out of size classes: fall back to fresh arena allocs
}

static void *ws_alloc(vv_NodePool *p, uint32_t size) {
    uint32_t r = ws_round(size);
    vv_WSBucket *b = ws_bucket(p, r);
    if (b && b->head) { void *blk = b->head; b->head = *(void **)blk; return blk; }
    // First few bytes must be able to hold a freelist pointer on release.
    return vv_arena_alloc(p->persistent, r < sizeof(void *) ? sizeof(void *) : r);
}

static void ws_free(vv_NodePool *p, void *blk, uint32_t size) {
    vv_WSBucket *b = ws_bucket(p, ws_round(size));
    if (!b) return; // leaked into the arena; bounded and rare
    *(void **)blk = b->head;
    b->head = blk;
}

void *vv_pool_state(vv_NodePool *p, uint32_t index, uint32_t size) {
    vv_Node *n = &p->nodes[index];
    if (n->widget_state && n->widget_state_size == size) return n->widget_state;
    if (n->widget_state) ws_free(p, n->widget_state, n->widget_state_size);
    void *blk = ws_alloc(p, size);
    memset(blk, 0, size);
    n->widget_state      = blk;
    n->widget_state_size = size;
    return blk;
}

void vv_pool_free(vv_NodePool *p, uint32_t index) {
    vv_Node *n = &p->nodes[index];
    assert((n->flags & VV_FLAG_ALIVE) && "double free of node");
    map_remove(p, n->id);
    if (n->widget_state) ws_free(p, n->widget_state, n->widget_state_size);
    n->widget_state      = NULL;
    n->widget_state_size = 0;
    n->flags             = 0;         // clears ALIVE
    n->parent            = p->free_head; // thread onto freelist
    p->free_head         = index;
}
