#include "verve/vv_context.h"
#include "verve/vv_internal.h"

// Input (§11). Three states, deliberately distinct:
//   hovered — pointer over actual_rect, nothing above claims it
//   active  — pressed with capture; survives the pointer leaving (sliders)
//   focused — keyboard target; exactly one or none

// Topmost node under `p`: the last node in pre-order (= painted last = on top)
// whose actual_rect contains p and which participates in hit testing. Disabled
// subtrees and exiting corpses are skipped (§3.3, §5.2).
static void hit_test(vv_NodePool *pool, uint32_t idx, vv_Vec2 p,
                     bool disabled, vv_ID *out) {
    vv_Node *n = vv_pool_get(pool, idx);
    bool dis = disabled || n->decl.disabled;

    if (!(n->flags & VV_FLAG_EXITING) && !dis && vv_rect_contains(n->actual_rect, p))
        *out = n->id;   // later writer wins => topmost

    // Clip: children outside a clipping rect can't be hit.
    bool clipped = (n->decl.clip || n->decl.scroll_x || n->decl.scroll_y) &&
                   !vv_rect_contains(n->actual_rect, p);
    if (clipped) return;

    for (uint32_t c = n->first_child; c != VV_NIL; c = vv_pool_get(pool, c)->next_sibling)
        hit_test(pool, c, p, dis, out);
}

static vv_Node *by_id(vv_Ctx *ctx, vv_ID id) {
    if (!id) return NULL;
    uint32_t idx = vv_pool_find(&ctx->pool, id);
    return idx == VV_NIL ? NULL : vv_pool_get(&ctx->pool, idx);
}

// Route wheel to the hovered node or its nearest scrolling ancestor.
static void apply_wheel(vv_Ctx *ctx, float wheel) {
    if (wheel == 0.0f) return;
    for (vv_Node *n = by_id(ctx, ctx->hovered_id); n; ) {
        if (n->decl.scroll_y && n->scroll_max_y > 0.0f) {
            float t = vv_clampf(n->scroll_y.target - wheel * 40.0f, 0.0f, n->scroll_max_y);
            vv_spring_retarget(&n->scroll_y, t);
            return;
        }
        if (n->decl.scroll_x && n->scroll_max_x > 0.0f) {
            float t = vv_clampf(n->scroll_x.target - wheel * 40.0f, 0.0f, n->scroll_max_x);
            vv_spring_retarget(&n->scroll_x, t);
            return;
        }
        n = n->parent == VV_NIL ? NULL : vv_pool_get(&ctx->pool, n->parent);
    }
}

void vv_input_process(vv_Ctx *ctx) {
    vv_Vec2 m = ctx->input.mouse;
    bool down = ctx->input.mouse_down;
    bool was  = ctx->mouse_prev_down;

    ctx->clicked_id = 0;
    ctx->pressed_id = 0;

    // 1) Hover: fresh hit test against last frame's geometry. While a node is
    // captured (active), it keeps the pointer regardless of position (§11.2).
    if (ctx->active_id && down) {
        // captured: hover reflects capture target for styling continuity
        ctx->hovered_id = ctx->active_id;
    } else {
        vv_ID hit = 0;
        hit_test(&ctx->pool, ctx->root, m, false, &hit);
        ctx->hovered_id = hit;
    }

    // 2) Press edge.
    if (down && !was) {
        ctx->active_id  = ctx->hovered_id;   // capture
        ctx->pressed_id = ctx->hovered_id;
        ctx->drag_start = m;
        ctx->drag_delta = vv_v2(0, 0);
        // Focus follows press on focusable nodes; else focus clears.
        vv_Node *hn = by_id(ctx, ctx->hovered_id);
        if (hn && hn->decl.focusable && !hn->decl.disabled)
            ctx->focused_id = hn->id;
        else if (hn)
            ctx->focused_id = 0;
    }

    // 3) Drag while held.
    if (down && ctx->active_id) {
        ctx->drag_delta = vv_v2(m.x - ctx->drag_start.x, m.y - ctx->drag_start.y);
    }

    // 4) Release edge: click iff release lands on the captured node.
    if (!down && was) {
        vv_ID topmost = 0;
        hit_test(&ctx->pool, ctx->root, m, false, &topmost);
        if (ctx->active_id && topmost == ctx->active_id)
            ctx->clicked_id = ctx->active_id;
        ctx->active_id = 0;
    }

    apply_wheel(ctx, ctx->input.wheel);

    // Maintain per-node interaction flags for declarative variants (§4.4) and
    // the animation system to key off, without build code querying anything.
    for (uint32_t i = 0; i < ctx->pool.count; i++) {
        vv_Node *n = &ctx->pool.nodes[i];
        if (!(n->flags & VV_FLAG_ALIVE)) continue;
        uint32_t f = n->flags & ~(uint32_t)(VV_FLAG_HOVERED | VV_FLAG_ACTIVE | VV_FLAG_FOCUSED);
        if (n->id == ctx->hovered_id) f |= VV_FLAG_HOVERED;
        if (n->id == ctx->active_id)  f |= VV_FLAG_ACTIVE;
        if (n->id == ctx->focused_id) f |= VV_FLAG_FOCUSED;
        n->flags = f;
    }

    ctx->mouse_prev_down = down;
}

// ---- queries --------------------------------------------------------------

static vv_ID id_of(vv_Ctx *ctx, uint32_t index) {
    vv_Node *n = vv_pool_get(&ctx->pool, index);
    return n ? n->id : 0;
}

bool vv_hovered(vv_Ctx *ctx, uint32_t index) { return id_of(ctx, index) == ctx->hovered_id && ctx->hovered_id; }
bool vv_pressed(vv_Ctx *ctx, uint32_t index) { return id_of(ctx, index) == ctx->pressed_id && ctx->pressed_id; }
bool vv_clicked(vv_Ctx *ctx, uint32_t index) { return id_of(ctx, index) == ctx->clicked_id && ctx->clicked_id; }
bool vv_active(vv_Ctx *ctx, uint32_t index)  { return id_of(ctx, index) == ctx->active_id  && ctx->active_id; }
bool vv_focused(vv_Ctx *ctx, uint32_t index) { return id_of(ctx, index) == ctx->focused_id && ctx->focused_id; }

vv_Vec2 vv_drag_delta(vv_Ctx *ctx, uint32_t index) {
    return vv_active(ctx, index) ? ctx->drag_delta : vv_v2(0, 0);
}

void vv_focus(vv_Ctx *ctx, uint32_t index) {
    vv_Node *n = vv_pool_get(&ctx->pool, index);
    if (n) ctx->focused_id = n->id;
}

// Note: build code that calls vv_hovered() opts the node out of pure Present
// tiering; the reconciler flags it (§4.4). Wired when idle mode lands (Phase 10).
