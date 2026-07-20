// vv_node.h — persistent retained node + the pool that owns them (§3.2, §3.3).
//
// Nodes live in a pool with a freelist; an open-addressing map (ID -> index)
// provides lookup. Both survive across frames. The tree is expressed with pool
// indices, never pointers, because pool growth reallocates (§13).
#ifndef VV_NODE_H
#define VV_NODE_H

#include "vv_arena.h"
#include "vv_event.h"
#include "vv_layout.h"
#include "vv_style.h"

#define VV_NIL 0xFFFFFFFFu   // "no node" sentinel for index links

typedef enum {
    VV_FLAG_HOVERED  = 1u << 0,
    VV_FLAG_ACTIVE   = 1u << 1,
    VV_FLAG_FOCUSED  = 1u << 2,
    VV_FLAG_EXITING  = 1u << 3,
    VV_FLAG_CLIP     = 1u << 4,
    VV_FLAG_SCROLL   = 1u << 5,
    VV_FLAG_ALIVE    = 1u << 6,  // slot is occupied
    VV_FLAG_CULLED   = 1u << 7,  // freed by virtualization; skip exit anim (§5.5)
    VV_FLAG_REBUILD_ON_INTERACTION = 1u << 8, // queried vv_hovered() in build (§4.4)
    VV_FLAG_TEXT     = 1u << 9,  // leaf carrying a string in widget_state
} vv_NodeFlags;

typedef struct vv_Node {
    vv_ID    id;
    uint32_t parent, first_child, last_child, next_sibling; // pool indices
    uint32_t child_count;
    uint32_t seq;                 // sequence index among siblings this frame
    uint64_t last_touched_frame;

    vv_LayoutDecl decl;           // this frame's declared layout intent
    vv_Style      target;         // this frame's declared style
    vv_StyleAnim  actual;         // interpolated values + velocities

    vv_Rect  layout_rect;         // where layout says it goes
    vv_Rect  actual_rect;         // where it's drawn (springs toward layout_rect)
    vv_Rect  prev_layout_rect;    // for FLIP
    vv_Spring rx, ry, rw, rh;     // FLIP springs for actual_rect

    vv_Spring enter, exit;        // 0->1 lifecycle springs
    uint32_t  flags;

    // Set during build if this node subscribed to pointer-move (vv_On.move).
    // Read next frame's input step: while hovered, a move emits this message
    // (carrying the cursor) and forces a rebuild — the opt-in for cursor-driven
    // views that the plain hover-change gate skips. Cleared each build.
    vv_Msg    on_move;

    void    *widget_state;
    uint32_t widget_state_size;

    // Custom-draw leaf (§14.3): when set, present emits a VV_CMD_CUSTOM with this
    // node's actual_rect instead of a fill. Points at app-persistent memory.
    const vv_CustomDraw *custom;

    // Vector draw-list (§14.5): frame-arena polylines/polygons in local space,
    // attached by the vv_draw_* builders. Present lowers each to a VV_CMD_POLY.
    const struct vv_DrawList *draw;

    // Image leaf: when set, present emits a VV_CMD_IMAGE over the node's rect.
    const vv_ImageRef *image;

    // Text leaves stash their frame-arena string copy here (separate from
    // widget_state, which the freelist allocator owns).
    const char *text;
    uint32_t    text_len;

    // Leaf measurement (§14.2 item 4, §15). Composite nodes derive size from
    // children; a leaf either uses FIXED sizes or installs a measure callback.
    // Returns the content size given a wrap width (height-for-width, §5.3).
    vv_Vec2 (*measure)(void *ud, float wrap_width, const struct vv_Node *n);
    void    *measure_ud;

    // Layout scratch, recomputed every Build. Intrinsic ("fit") sizes from the
    // bottom-up passes, before top-down distribution.
    float fit_w, fit_h;

    // Springy scroll (§5.6): target snaps, actual glides. Offsets are >= 0 and
    // subtracted from child positions. Content extents are measured in layout.
    vv_Spring scroll_x, scroll_y;
    float     scroll_max_x, scroll_max_y;
    float     scroll_activity; // 1 while scrolling/dragging, decays -> scrollbar fade
} vv_Node;

typedef struct {
    vv_ID    id;      // 0 = empty slot
    uint32_t index;   // pool index
} vv_MapSlot;

// Size-bucketed freelist for widget_state so a dead node's state returns for
// reuse without malloc (§13, §14.2). Blocks are rounded to a size class.
#define VV_WS_BUCKETS 24
typedef struct { uint32_t size; void *head; } vv_WSBucket;

typedef struct vv_NodePool {
    vv_Node *nodes;
    uint32_t count;      // high-water mark of allocated slots
    uint32_t cap;
    uint32_t free_head;  // freelist head (index), VV_NIL if empty

    vv_MapSlot *map;     // open-addressing ID -> index
    uint32_t    map_cap; // power of two
    uint32_t    map_len; // live entries

    vv_WSBucket ws[VV_WS_BUCKETS];
    int         ws_count;

    vv_Arena *persistent; // backing store for pool + widget_state
} vv_NodePool;

void      vv_pool_init(vv_NodePool *p, vv_Arena *persistent, uint32_t initial_cap);

// Look up by ID. Returns VV_NIL if absent.
uint32_t  vv_pool_find(vv_NodePool *p, vv_ID id);

// Get-or-create. `created` (nullable) is set true on birth. Returns pool index.
uint32_t  vv_pool_obtain(vv_NodePool *p, vv_ID id, bool *created);

// Free a node slot and its widget_state, remove from map.
void      vv_pool_free(vv_NodePool *p, uint32_t index);

// Persistent per-node state (§14.2). Zeroed on first use; kept across frames;
// returned to the freelist when the node dies. Returns the same block each
// frame for a given (node, size).
void     *vv_pool_state(vv_NodePool *p, uint32_t index, uint32_t size);

static inline vv_Node *vv_pool_get(vv_NodePool *p, uint32_t index) {
    return index == VV_NIL ? NULL : &p->nodes[index];
}

#endif // VV_NODE_H
