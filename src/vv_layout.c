#include "verve/vv_layout.h"
#include "verve/vv_node.h"

// Phase 0 placeholder layout.
//
// The real 4-pass constraint solver (§5) lands in Phase 1 and will replace the
// body of vv_layout_run wholesale — the module seam is deliberate. For now we
// give every node a concrete rect so the pipeline downstream (animate, hit
// test, emit) has something real to operate on and reconciliation is visible:
//
//   * A node with `has_absolute` is placed at its absolute rect, offset by the
//     parent's content origin.
//   * Otherwise FIXED sizes stack along the parent's main axis with gap/pad.
//   * FIT/GROW/PERCENT collapse to their `value` (or 0) until Phase 1.

static float size_px(vv_Size s, float parent_extent) {
    switch (s.mode) {
        case VV_SIZE_FIXED:   return s.value;
        case VV_SIZE_PERCENT: return parent_extent * s.value;
        case VV_SIZE_GROW:    return 0.0f; // resolved by Phase 1
        case VV_SIZE_FIT:     default: return s.value; // treat as hint for now
    }
}

static void layout_node(vv_NodePool *p, uint32_t index, vv_Rect avail);

static void layout_children(vv_NodePool *p, uint32_t parent_index, vv_Rect content) {
    vv_Node *parent = vv_pool_get(p, parent_index);
    bool row = parent->decl.dir == VV_ROW;
    float cursor = row ? content.x : content.y;

    for (uint32_t c = parent->first_child; c != VV_NIL;) {
        vv_Node *child = vv_pool_get(p, c);
        uint32_t next  = child->next_sibling;

        if (child->decl.has_absolute) {
            vv_Rect a = child->decl.absolute;
            layout_node(p, c, vv_rect(content.x + a.x, content.y + a.y, a.w, a.h));
        } else {
            float w = size_px(child->decl.w, content.w);
            float h = size_px(child->decl.h, content.h);
            vv_Rect r = row ? vv_rect(cursor, content.y, w, h)
                            : vv_rect(content.x, cursor, w, h);
            layout_node(p, c, r);
            cursor += (row ? w : h) + parent->decl.gap;
        }
        c = next;
    }
}

static void layout_node(vv_NodePool *p, uint32_t index, vv_Rect avail) {
    vv_Node *n = vv_pool_get(p, index);
    n->prev_layout_rect = n->layout_rect;
    n->layout_rect      = avail;

    vv_Edges pad = n->decl.padding;
    vv_Rect content = vv_rect(avail.x + pad.l, avail.y + pad.t,
                              avail.w - pad.l - pad.r,
                              avail.h - pad.t - pad.b);
    layout_children(p, index, content);
}

void vv_layout_run(vv_NodePool *p, uint32_t root, vv_Rect window) {
    layout_node(p, root, window);
}
