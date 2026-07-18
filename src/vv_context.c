#include "verve/vv_context.h"
#include "verve/vv_internal.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ROOT_ID ((vv_ID)0x9e3779b97f4a7c15ULL)

// ---- setup ---------------------------------------------------------------

void vv_init(vv_Ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    vv_arena_init(&ctx->persistent, 1 << 20); // 1 MiB starter block
    vv_arena_init(&ctx->frame, 1 << 18);      // 256 KiB, reset on build frames
    vv_arena_init(&ctx->present, 1 << 18);    // 256 KiB, reset every frame
    vv_pool_init(&ctx->pool, &ctx->persistent, 256);

    ctx->animation_scale = 1.0f;
    ctx->dpi_scale       = 1.0f;
    ctx->win_w           = 800;
    ctx->win_h           = 600;
    ctx->tree_dirty      = true;

    bool created;
    ctx->root = vv_pool_obtain(&ctx->pool, ROOT_ID, &created);
}

void vv_shutdown(vv_Ctx *ctx) {
    vv_arena_destroy(&ctx->persistent);
    vv_arena_destroy(&ctx->frame);
    vv_arena_destroy(&ctx->present);
    memset(ctx, 0, sizeof(*ctx));
}

void vv_set_window(vv_Ctx *ctx, float w, float h, float dpi_scale) {
    ctx->win_w = w; ctx->win_h = h; ctx->dpi_scale = dpi_scale;
}
void vv_set_measure_fn(vv_Ctx *ctx,
                       vv_Vec2 (*fn)(void *ud, const char *s, int len,
                                     vv_FontID font, float size, float wrap_width),
                       void *ud) {
    ctx->measure_text = fn; ctx->measure_ud = ud;
}
void *vv_state_raw(vv_Ctx *ctx, uint32_t index, size_t size) {
    return vv_pool_state(&ctx->pool, index, (uint32_t)size);
}
void vv_set_idle_mode(vv_Ctx *ctx, bool on)         { ctx->idle_mode = on; }
void vv_set_animation_scale(vv_Ctx *ctx, float s)   { ctx->animation_scale = s; }
void vv_invalidate(vv_Ctx *ctx)                     { ctx->tree_dirty = true; }

// ---- declarative state variants (§4.4, §7.1) -----------------------------

// Overlay a sparse variant onto the base target. A field applies if its
// presence bit is set OR — as a convenience so users needn't fill the mask for
// the common case — a color's alpha is non-zero / the transform is non-identity
// (the sentinel fallback discussed in open question 1a).
static void overlay_variant(vv_Style *dst, const vv_Style *v) {
    if (!v) return;
    uint32_t s = v->set;
    if ((s & VV_STYLE_BG) || v->bg.a > 0.0f)                     dst->bg = v->bg;
    if ((s & VV_STYLE_FG) || v->fg.a > 0.0f)                     dst->fg = v->fg;
    if ((s & VV_STYLE_BORDER_COLOR) || v->border_color.a > 0.0f) dst->border_color = v->border_color;
    if ((s & VV_STYLE_SHADOW) || v->shadow.color.a > 0.0f)       dst->shadow = v->shadow;
    if (s & VV_STYLE_RADIUS)       dst->radius = v->radius;
    if (s & VV_STYLE_BORDER_WIDTH) dst->border_width = v->border_width;
    if ((s & VV_STYLE_OPACITY) || v->opacity > 0.0f)             dst->opacity = v->opacity;
    bool has_xform = (s & VV_STYLE_TRANSFORM) ||
                     v->transform.a != 0.0f || v->transform.b != 0.0f ||
                     v->transform.c != 0.0f || v->transform.d != 0.0f ||
                     v->transform.tx != 0.0f || v->transform.ty != 0.0f;
    if (has_xform) dst->transform = v->transform;
}

// Fold interaction variants into `target` at build time, while the variant
// pointers (often stack compound literals, §14.1) are still valid. Order:
// disabled -> focus -> hover -> active, later winning (§7.1). Interaction flags
// come from this frame's input pass (last frame's geometry, the §4.5 lag).
static void resolve_variants(vv_Node *n) {
    const vv_Style *hover = n->target.hover, *active = n->target.active;
    const vv_Style *focus = n->target.focus, *disabled = n->target.disabled;
    if (n->decl.disabled)          overlay_variant(&n->target, disabled);
    if (n->flags & VV_FLAG_FOCUSED) overlay_variant(&n->target, focus);
    if (n->flags & VV_FLAG_HOVERED) overlay_variant(&n->target, hover);
    if (n->flags & VV_FLAG_ACTIVE)  overlay_variant(&n->target, active);
    // Pointers consumed; Present reads only the resolved target.
    n->target.hover = n->target.active = n->target.focus = n->target.disabled = NULL;
}

// ---- build API -----------------------------------------------------------

static uint32_t open_node(vv_Ctx *ctx, const char *key, size_t klen,
                          vv_LayoutDecl decl, vv_Style style) {
    assert(ctx->in_build && "build calls must be between begin/end_frame");
    assert(ctx->depth > 0 && "no open parent");

    uint32_t parent_idx = ctx->stack[ctx->depth - 1];
    uint32_t seq        = ctx->seq_counter[ctx->depth - 1]++;
    vv_Node *parent     = vv_pool_get(&ctx->pool, parent_idx);
    vv_ID    id         = vv_id(parent->id, seq, key, klen);

    bool created;
    uint32_t idx = vv_pool_obtain(&ctx->pool, id, &created);
    vv_Node *n   = vv_pool_get(&ctx->pool, idx);

    // Revive if it was exiting; refresh declared intent.
    n->flags &= ~(uint32_t)VV_FLAG_EXITING;
    n->flags |= VV_FLAG_ALIVE;
    n->parent             = parent_idx;
    n->seq                = seq;
    n->decl               = decl;
    n->target             = style;
    resolve_variants(n);  // fold hover/active/focus/disabled into target now
    if (ctx->focus_next && decl.focusable && !decl.disabled) {
        ctx->focused_id = n->id;
        ctx->focus_next = false;
    }
    n->last_touched_frame = ctx->frame_index;
    n->first_child = n->last_child = VV_NIL;
    n->next_sibling = VV_NIL;
    n->child_count  = 0;

    // Append to parent's freshly-reset child list.
    if (parent->first_child == VV_NIL) {
        parent->first_child = parent->last_child = idx;
    } else {
        vv_pool_get(&ctx->pool, parent->last_child)->next_sibling = idx;
        parent->last_child = idx;
    }
    parent->child_count++;

    return idx;
}

uint32_t vv_box_keyed(vv_Ctx *ctx, const char *key, size_t klen,
                      vv_LayoutDecl decl, vv_Style style) {
    uint32_t idx = open_node(ctx, key, klen, decl, style);
    assert(ctx->depth < VV_BUILD_STACK_MAX && "build stack overflow");
    ctx->stack[ctx->depth]       = idx;
    ctx->seq_counter[ctx->depth] = 0;
    ctx->depth++;
    return idx;
}

void vv_end_box(vv_Ctx *ctx) {
    assert(ctx->depth > 1 && "unbalanced vv_end_box");
    ctx->depth--;
}

void vv_rows(vv_Ctx *ctx, int count, float row_h,
             void (*fn)(vv_Ctx *ctx, int index, void *ud), void *ud) {
    if (count <= 0 || row_h <= 0.0f) return;

    // Find the nearest enclosing scroll_y container on the build stack.
    float voff = 0.0f, vh = ctx->win_h;
    for (int d = ctx->depth - 1; d >= 0; d--) {
        vv_Node *n = vv_pool_get(&ctx->pool, ctx->stack[d]);
        if (n->decl.scroll_y) {
            voff = n->scroll_y.x;
            vh   = n->actual_rect.h > 0 ? n->actual_rect.h : ctx->win_h;
            break;
        }
    }

    const int overscan = 2;
    int first = (int)floorf(voff / row_h) - overscan;
    int last  = (int)ceilf((voff + vh) / row_h) + overscan;
    if (first < 0) first = 0;
    if (last > count) last = count;
    if (last < first) last = first;

    if (first > 0) { // spacer above
        vv_box(ctx, (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_fixed((float)first * row_h) },
               (vv_Style){0});
        vv_end_box(ctx);
    }
    for (int i = first; i < last; i++) {
        char key[24];
        int klen = snprintf(key, sizeof key, "\x01row%d", i);
        uint32_t row = vv_box_keyed(ctx, key, (size_t)klen,
            (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_fixed(row_h) }, (vv_Style){0});
        // Mark culled-on-untouch: scrolled-away rows free immediately, bypassing
        // the exit spring so a scroll leaves no fading corpses (§5.5).
        vv_pool_get(&ctx->pool, row)->flags |= VV_FLAG_CULLED;
        fn(ctx, i, ud);
        vv_end_box(ctx);
    }
    if (last < count) { // spacer below
        vv_box(ctx, (vv_LayoutDecl){ .w = vv_grow(1),
                                     .h = vv_fixed((float)(count - last) * row_h) },
               (vv_Style){0});
        vv_end_box(ctx);
    }
}

vv_Str vv_fmt(vv_Ctx *ctx, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vv_Str s = vv_str_vformat(&ctx->frame, fmt, ap);
    va_end(ap);
    return s;
}

uint32_t vv_text_keyed(vv_Ctx *ctx, const char *key, size_t klen,
                       const char *utf8, vv_Style style) {
    // A text leaf: copy the string into the frame arena so the core never holds
    // a reference into caller (or Lua GC) memory past the frame (§13).
    vv_LayoutDecl decl = (vv_LayoutDecl){0};
    size_t len = utf8 ? strlen(utf8) : 0;
    char  *copy = vv_arena_alloc(&ctx->frame, len + 1);
    if (len) memcpy(copy, utf8, len);
    copy[len] = 0;

    uint32_t idx = open_node(ctx, key, klen, decl, style);
    vv_Node *n = vv_pool_get(&ctx->pool, idx);
    n->text     = copy;
    n->text_len = (uint32_t)len;
    n->flags |= VV_FLAG_TEXT;
    return idx;
}

// ---- reconciliation ------------------------------------------------------

// Sweep the whole pool. Any ALIVE node not touched this frame is dying: mark it
// EXITING (§3.3). Any EXITING node whose exit spring has settled is freed. This
// is O(pool) but pool size is bounded by the freelist; Phase 5+ can tighten it.
static void reconcile(vv_Ctx *ctx) {
    vv_NodePool *p = &ctx->pool;
    for (uint32_t i = 0; i < p->count; i++) {
        vv_Node *n = &p->nodes[i];
        if (!(n->flags & VV_FLAG_ALIVE)) continue;
        if (i == ctx->root) continue;

        if (n->last_touched_frame != ctx->frame_index) {
            if (n->flags & VV_FLAG_CULLED) {
                // Removed by virtualization: free immediately, no exit anim (§5.5).
                vv_pool_free(p, i);
            } else if (!(n->flags & VV_FLAG_EXITING)) {
                n->flags |= VV_FLAG_EXITING;
                vv_spring_init(&n->exit, 1.0f, VV_DEFAULT_SPRING);
                vv_spring_retarget(&n->exit, 0.0f);
            } else if (n->exit.settled) {
                vv_pool_free(p, i);
            }
        }
    }
}

// ---- present: animate + emit ---------------------------------------------

// ---- frame ---------------------------------------------------------------

// Whether this frame's input could change the tree, so a rebuild is warranted
// (the idle gate, §4.2). Movement is included so hover variants/hover events
// react; a held capture keeps building so drags track. Pure-idle frames (no
// movement, no buttons/keys) fall through to present-only.
static bool input_is_interactive(vv_Ctx *ctx) {
    const vv_Input *in = &ctx->input;
    bool moved = in->mouse.x != ctx->mouse_prev.x || in->mouse.y != ctx->mouse_prev.y;
    return moved || in->mouse_down || ctx->mouse_prev_down ||
           in->wheel != 0.0f || in->key_count > 0 || in->text_len > 0 ||
           ctx->active_id != 0;
}

// Step 1: consume input and prepare a fresh command buffer. Does NOT reset the
// frame arena or the tree — so a present-only frame can follow without losing
// the last build's node text (which lives in the frame arena).
static void input_step(vv_Ctx *ctx, float dt, const vv_Input *input) {
    ctx->frame_index++;
    ctx->dt = dt;
    ctx->clock += dt;
    if (input) ctx->input = *input;

    vv_arena_reset(&ctx->present);
    ctx->cmds = (vv_CommandBuffer){0};
    ctx->unsettled_springs = 0;

    ctx->wants_build = input_is_interactive(ctx) || ctx->tree_dirty ||
                       ctx->frame_index == 1;

    // Resolve input against last frame's geometry before build code queries it.
    vv_input_process(ctx);
}

// Step 2: reset the root and build stack so view code can populate the tree.
// Resets the frame arena — the previous build's text/scratch is dead once we
// rebuild.
static void build_begin(vv_Ctx *ctx) {
    vv_arena_reset(&ctx->frame);

    vv_Node *root = vv_pool_get(&ctx->pool, ctx->root);
    root->first_child = root->last_child = VV_NIL;
    root->child_count = 0;
    root->last_touched_frame = ctx->frame_index;
    root->decl = (vv_LayoutDecl){ .dir = VV_COLUMN };
    root->layout_rect = root->actual_rect = vv_rect(0, 0, ctx->win_w, ctx->win_h);

    ctx->depth          = 1;
    ctx->stack[0]       = ctx->root;
    ctx->seq_counter[0] = 0;
    ctx->in_build       = true;
}

void vv_begin_frame(vv_Ctx *ctx, float dt, const vv_Input *input) {
    input_step(ctx, dt, input);
    build_begin(ctx);
}

vv_CommandBuffer *vv_end_frame(vv_Ctx *ctx) {
    assert(ctx->depth == 1 && "unbalanced build stack at end_frame");
    ctx->in_build = false;

    // --- Build phase (§4.1 steps 1-5) ---
    reconcile(ctx);
    vv_layout_run(ctx);

    // --- Present phase (§4.1 steps 6-8) ---
    vv_present(ctx);

    ctx->tree_dirty = false;
    ctx->last_tier  = VV_TIER_BUILD;
    return &ctx->cmds;
}

// Present-only frame: no build, no reconcile, no layout — just advance springs
// and re-emit the existing tree. Used when nothing changed but animations are
// still running (§4.2). The frame arena is untouched, so node text stays valid.
static vv_CommandBuffer *present_only(vv_Ctx *ctx) {
    vv_present(ctx);
    ctx->last_tier = VV_TIER_PRESENT;
    return &ctx->cmds;
}

vv_CommandBuffer *vv_run_frame(vv_Ctx *ctx, float dt, const vv_Input *input,
                               vv_UpdateFn update, vv_ViewFn view, void *state) {
    input_step(ctx, dt, input);

    // Drain messages emitted by the previous view() into the app's update().
    bool changed = false;
    if (update) {
        vv_Event ev;
        while (vv_poll_event(ctx, &ev)) { update(state, ev); changed = true; }
    }

    // Rebuild when state changed or this frame's input could emit a message;
    // otherwise present-only to keep animations advancing. (One-frame pipeline:
    // messages from this build are processed next frame. To switch to a settle
    // loop, this is the single place that would loop view()+drain until quiet.)
    if (changed || ctx->wants_build) {
        build_begin(ctx);
        if (view) view(ctx, state);
        return vv_end_frame(ctx);
    }
    return present_only(ctx);
}

// ---- message queue --------------------------------------------------------

void vv_emit(vv_Ctx *ctx, vv_Msg msg, vv_Payload data) {
    if (msg == VV_MSG_NONE) return;
    if (ctx->event_count >= VV_EVENT_CAP) {
        assert(0 && "event queue overflow — draining too slowly");
        return; // drop rather than clobber unread events
    }
    uint32_t slot = (ctx->event_head + ctx->event_count) % VV_EVENT_CAP;
    ctx->events[slot] = (vv_Event){ .msg = msg, .data = data };
    ctx->event_count++;
}

bool vv_poll_event(vv_Ctx *ctx, vv_Event *out) {
    if (ctx->event_count == 0) return false;
    if (out) *out = ctx->events[ctx->event_head];
    ctx->event_head = (ctx->event_head + 1) % VV_EVENT_CAP;
    ctx->event_count--;
    return true;
}
