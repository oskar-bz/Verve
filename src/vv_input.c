#include "verve/vv_context.h"
#include "verve/vv_internal.h"

// Input (§11). Three states, deliberately distinct:
//   hovered — pointer over actual_rect, nothing above claims it
//   active  — pressed with capture; survives the pointer leaving (sliders)
//   focused — keyboard target; exactly one or none

// Topmost node under `p`: the last node in pre-order (= painted last = on top)
// whose actual_rect contains p and which participates in hit testing. Disabled
// subtrees and exiting corpses are skipped (§3.3, §5.2).
//
// Overlays (decl.z > 0) paint in a later pass above the tree, so hit testing
// must agree or a click would fall through to content painted underneath. When
// `collect` is set the main walk skips z>0 subtrees, recording their roots; the
// caller then re-tests them in ascending z (paint order) so the topmost overlay
// claims the hit. `nov` caps at the overlay array size.
static void hit_test(vv_NodePool *pool, uint32_t idx, vv_Vec2 p,
                     bool disabled, vv_ID *out,
                     bool collect, uint32_t *ov, int *nov, int ov_cap) {
    vv_Node *n = vv_pool_get(pool, idx);

    if (collect && n->decl.z > 0) {           // an overlay root: defer it
        if (*nov < ov_cap) ov[(*nov)++] = idx;
        return;
    }
    bool dis = disabled || n->decl.disabled;

    // Text is decorative and pass-through, so a label inside a button doesn't
    // steal the button's hit.
    if (!(n->flags & (VV_FLAG_EXITING | VV_FLAG_TEXT)) && !dis &&
        vv_rect_contains(n->actual_rect, p))
        *out = n->id;   // later writer wins => topmost

    // Clip: children outside a clipping rect can't be hit.
    bool clipped = (n->decl.clip || n->decl.scroll_x || n->decl.scroll_y) &&
                   !vv_rect_contains(n->actual_rect, p);
    if (clipped) return;

    for (uint32_t c = n->first_child; c != VV_NIL; c = vv_pool_get(pool, c)->next_sibling)
        hit_test(pool, c, p, dis, out, collect, ov, nov, ov_cap);
}

static vv_Node *by_id(vv_Ctx *ctx, vv_ID id) {
    if (!id) return NULL;
    uint32_t idx = vv_pool_find(&ctx->pool, id);
    return idx == VV_NIL ? NULL : vv_pool_get(&ctx->pool, idx);
}

// Resolve a raw topmost hit to its interaction target: the nearest focusable
// node at or above it, so a click on a widget's decorative child (a knob, an
// icon) routes to the widget. Falls back to the topmost if nothing focusable.
static vv_ID interactive_target(vv_Ctx *ctx, vv_ID topmost) {
    uint32_t idx = topmost ? vv_pool_find(&ctx->pool, topmost) : VV_NIL;
    while (idx != VV_NIL) {
        vv_Node *n = &ctx->pool.nodes[idx];
        if (n->decl.focusable) return n->id;
        idx = n->parent;
    }
    return topmost;
}

// The raw topmost node under `p`, without resolving to a focusable target.
static vv_ID hit_target_raw(vv_Ctx *ctx, vv_Vec2 p) {
    vv_ID hit = 0;
    uint32_t ov[64]; int nov = 0;
    // Pass 1: the normal tree, collecting overlay roots (z>0) instead of
    // descending them.
    hit_test(&ctx->pool, ctx->root, p, false, &hit, true, ov, &nov, 64);
    // Pass 2: overlays in ascending z (stable insertion sort keeps build order
    // among equal z), each overwriting the hit — mirroring the paint order.
    for (int i = 1; i < nov; i++) {
        uint32_t v = ov[i];
        int z = vv_pool_get(&ctx->pool, v)->decl.z, j = i - 1;
        while (j >= 0 && vv_pool_get(&ctx->pool, ov[j])->decl.z > z) { ov[j + 1] = ov[j]; j--; }
        ov[j + 1] = v;
    }
    for (int i = 0; i < nov; i++)
        hit_test(&ctx->pool, ov[i], p, false, &hit, false, NULL, NULL, 0);
    return hit;
}

static vv_ID hit_target(vv_Ctx *ctx, vv_Vec2 p) {
    return interactive_target(ctx, hit_target_raw(ctx, p));
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

// Collect focusable node ids in tree (pre-order) for Tab traversal (§11.3).
static void collect_focusable(vv_NodePool *pool, uint32_t idx, bool disabled,
                              vv_ID *list, int *n, int cap) {
    vv_Node *node = vv_pool_get(pool, idx);
    bool dis = disabled || node->decl.disabled;
    if (!(node->flags & VV_FLAG_EXITING) && !dis && node->decl.focusable && *n < cap)
        list[(*n)++] = node->id;
    for (uint32_t c = node->first_child; c != VV_NIL; c = vv_pool_get(pool, c)->next_sibling)
        collect_focusable(pool, c, dis, list, n, cap);
}

static void tab_move(vv_Ctx *ctx, bool backward) {
    vv_ID list[256]; int n = 0;
    collect_focusable(&ctx->pool, ctx->root, false, list, &n, 256);
    if (n == 0) return;
    int cur = -1;
    for (int i = 0; i < n; i++) if (list[i] == ctx->focused_id) cur = i;
    int nxt = cur < 0 ? (backward ? n - 1 : 0)
                      : ((cur + (backward ? -1 : 1)) % n + n) % n;
    ctx->focused_id = list[nxt];
}

// Topmost scroll container whose (widened) scrollbar thumb contains `p`. Sets
// *grab to the pointer's offset within the thumb so dragging tracks 1:1.
static vv_ID hit_scrollbar(vv_Ctx *ctx, vv_Vec2 p, float *grab) {
    vv_ID found = 0;
    for (uint32_t i = 0; i < ctx->pool.count; i++) {
        vv_Node *n = &ctx->pool.nodes[i];
        if (!(n->flags & VV_FLAG_ALIVE) || (n->flags & VV_FLAG_EXITING)) continue;
        vv_Rect thumb;
        if (!vv_scrollbar_thumb_v(n, &thumb)) continue;
        vv_Rect hit = vv_rect(thumb.x - 8, thumb.y, thumb.w + 12, thumb.h); // easier grab
        if (vv_rect_contains(hit, p)) { *grab = p.y - thumb.y; found = n->id; }
    }
    return found; // last (topmost) match wins
}

void vv_input_process(vv_Ctx *ctx) {
    vv_Vec2 m = ctx->input.mouse;
    bool down = ctx->input.mouse_down;
    bool was  = ctx->mouse_prev_down;

    ctx->clicked_id = 0;
    ctx->pressed_id = 0;
    ctx->double_clicked_id = 0;
    ctx->right_clicked_id = 0;

    // 1) Hover: fresh hit test against last frame's geometry. While a node is
    // captured (active) or a scrollbar thumb is being dragged, the pointer stays
    // with the captor regardless of position (§11.2).
    if (ctx->sb_drag && down) {
        ctx->hovered_id = ctx->sb_drag;
    } else if (ctx->active_id && down) {
        // captured: hover reflects capture target for styling continuity
        ctx->hovered_id = ctx->active_id;
    } else {
        ctx->hovered_id = hit_target(ctx, m);
    }

    // 2) Press edge. A press on a scrollbar thumb starts a thumb drag instead of
    // the normal capture, so the content underneath isn't clicked/selected.
    if (down && !was) {
        float grab = 0.0f;
        vv_ID sb = hit_scrollbar(ctx, m, &grab);
        if (sb) {
            ctx->sb_drag = sb;
            ctx->sb_grab = grab;
        } else {
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
    }

    // 3a) Scrollbar thumb drag: map the pointer to a scroll offset, tracking 1:1.
    if (down && ctx->sb_drag) {
        vv_Node *n = by_id(ctx, ctx->sb_drag);
        vv_Rect thumb;
        if (n && vv_scrollbar_thumb_v(n, &thumb)) {
            float avail = n->actual_rect.h - thumb.h;
            float top = m.y - ctx->sb_grab;
            float frac = avail > 0 ? vv_clampf((top - n->actual_rect.y) / avail, 0, 1) : 0;
            float t = frac * n->scroll_max_y;
            vv_spring_retarget(&n->scroll_y, t);
            n->scroll_y.x = t; n->scroll_y.v = 0; // snap for a direct grab feel
            n->scroll_activity = 1.0f;
        }
    }

    // 3b) Drag while held (normal capture).
    if (down && ctx->active_id) {
        ctx->drag_delta = vv_v2(m.x - ctx->drag_start.x, m.y - ctx->drag_start.y);
    }

    // 4) Release edge: click iff release lands on the captured node. A second
    // click on the same node within the window is a double-click (§11).
    if (!down && was && ctx->sb_drag) {
        ctx->sb_drag = 0; // end thumb drag; no click semantics
    } else if (!down && was) {
        if (ctx->active_id && hit_target(ctx, m) == ctx->active_id) {
            ctx->clicked_id = ctx->active_id;
            const float VV_DOUBLE_CLICK_S = 0.35f;
            if (ctx->last_click_id == ctx->clicked_id &&
                ctx->clock - ctx->last_click_time <= VV_DOUBLE_CLICK_S) {
                ctx->double_clicked_id = ctx->clicked_id;
                ctx->last_click_id = 0; // consume, so a triple isn't two doubles
            } else {
                ctx->last_click_id = ctx->clicked_id;
                ctx->last_click_time = ctx->clock;
            }
        }
        ctx->active_id = 0;
    }

    // Right button: a click is a press+release on the same node (no capture/drag
    // semantics; that's what context menus need).
    // Store the *raw* hit node (not its focusable target) so the click can
    // bubble: vv_right_clicked() matches the hit node or any ancestor, letting
    // an outer container's context menu fire even when an inner box was clicked.
    bool rdown = ctx->input.right_down, rwas = ctx->mouse_right_prev;
    if (rdown && !rwas) {
        ctx->right_active_id = hit_target_raw(ctx, m);
    } else if (!rdown && rwas) {
        if (ctx->right_active_id && hit_target_raw(ctx, m) == ctx->right_active_id)
            ctx->right_clicked_id = ctx->right_active_id;
        ctx->right_active_id = 0;
    }

    // Cursor shape: walk from the hovered node up to the first that requests one.
    ctx->cursor = VV_CURSOR_DEFAULT;
    for (uint32_t idx = ctx->hovered_id ? vv_pool_find(&ctx->pool, ctx->hovered_id) : VV_NIL;
         idx != VV_NIL; idx = ctx->pool.nodes[idx].parent) {
        if (ctx->pool.nodes[idx].decl.cursor != VV_CURSOR_DEFAULT) {
            ctx->cursor = ctx->pool.nodes[idx].decl.cursor;
            break;
        }
    }

    // Tab traversal follows tree order (§11.3). Handled here so the next build
    // reflects the new focus; the focused node walks last frame's tree.
    for (int i = 0; i < ctx->input.key_count; i++)
        if (ctx->input.keys[i].key == VV_KEY_TAB)
            tab_move(ctx, ctx->input.keys[i].shift);

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

    ctx->mouse_prev_down  = down;
    ctx->mouse_right_prev = ctx->input.right_down;
    ctx->mouse_prev       = m;
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
bool vv_double_clicked(vv_Ctx *ctx, uint32_t index) { return id_of(ctx, index) == ctx->double_clicked_id && ctx->double_clicked_id; }
// True if this frame's right-click landed on `index` or anywhere in its
// subtree — the event bubbles up, so a container handles clicks on its
// descendants without every inner box having to opt out.
bool vv_right_clicked(vv_Ctx *ctx, uint32_t index) {
    if (!ctx->right_clicked_id) return false;
    vv_ID want = id_of(ctx, index);
    if (!want) return false;
    uint32_t idx = vv_pool_find(&ctx->pool, ctx->right_clicked_id);
    while (idx != VV_NIL) {
        if (ctx->pool.nodes[idx].id == want) return true;
        idx = ctx->pool.nodes[idx].parent;
    }
    return false;
}
bool vv_activated(vv_Ctx *ctx, uint32_t index) {
    if (!ctx->focused_id || id_of(ctx, index) != ctx->focused_id) return false;
    for (int i = 0; i < ctx->input.key_count; i++)
        if (ctx->input.keys[i].key == VV_KEY_ENTER) return true;
    return false;
}

vv_Vec2 vv_drag_delta(vv_Ctx *ctx, uint32_t index) {
    return vv_active(ctx, index) ? ctx->drag_delta : vv_v2(0, 0);
}

// ---- drag and drop --------------------------------------------------------
bool vv_dnd_active(vv_Ctx *ctx) { return ctx->dnd_dragging; }

bool vv_drag_source(vv_Ctx *ctx, uint32_t index, vv_Payload payload) {
    vv_Node *n = vv_node(ctx, index);
    if (!n) return false;
    if (vv_active(ctx, index)) {
        vv_Vec2 d = ctx->drag_delta;
        if (ctx->dnd_dragging || d.x * d.x + d.y * d.y > 16.0f) { // 4px threshold
            ctx->dnd_dragging = true;
            ctx->dnd_source = n->id;
            ctx->dnd_payload = payload;
            return true; // this node is the live source
        }
    }
    return ctx->dnd_dragging && ctx->dnd_source == n->id;
}

bool vv_drop_target(vv_Ctx *ctx, uint32_t index, vv_Payload *out) {
    vv_Node *n = vv_node(ctx, index);
    if (!n || !ctx->dnd_dragging) return false;
    // The drop fires on the frame the button releases over a hovered target.
    bool released = ctx->mouse_prev_down && !ctx->input.mouse_down;
    if (released && vv_hovered(ctx, index) && ctx->dnd_source != n->id) {
        if (out) *out = ctx->dnd_payload;
        ctx->dnd_dragging = false; // consume
        return true;
    }
    return false;
}

void vv_focus(vv_Ctx *ctx, uint32_t index) {
    vv_Node *n = vv_pool_get(&ctx->pool, index);
    if (n) ctx->focused_id = n->id;
}

void vv_request_focus_next(vv_Ctx *ctx) { ctx->focus_next = true; }

// Note: build code that calls vv_hovered() opts the node out of pure Present
// tiering; the reconciler flags it (§4.4). Wired when idle mode lands (Phase 10).
