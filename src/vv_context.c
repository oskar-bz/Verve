#include "verve/vv_context.h"
#include "verve/vv_internal.h"

#include <assert.h>
#include <string.h>

#define ROOT_ID ((vv_ID)0x9e3779b97f4a7c15ULL)

// ---- setup ---------------------------------------------------------------

void vv_init(vv_Ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    vv_arena_init(&ctx->persistent, 1 << 20); // 1 MiB starter block
    vv_arena_init(&ctx->frame, 1 << 18);      // 256 KiB, reset each frame
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

void vv_begin_frame(vv_Ctx *ctx, float dt, const vv_Input *input) {
    ctx->frame_index++;
    ctx->dt = dt;
    if (input) ctx->input = *input;

    vv_arena_reset(&ctx->frame);
    ctx->cmds = (vv_CommandBuffer){0};
    ctx->unsettled_springs = 0;

    // Resolve input against last frame's geometry before build code queries it.
    vv_input_process(ctx);

    // Reset the root as this frame's build container.
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
