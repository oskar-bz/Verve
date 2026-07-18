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
void vv_set_idle_mode(vv_Ctx *ctx, bool on)         { ctx->idle_mode = on; }
void vv_set_animation_scale(vv_Ctx *ctx, float s)   { ctx->animation_scale = s; }
void vv_invalidate(vv_Ctx *ctx)                     { ctx->tree_dirty = true; }

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
    // Stash the frame-arena string pointer in widget_state for the emitter.
    n->widget_state      = copy;
    n->widget_state_size = (uint32_t)(len + 1);
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
    vv_layout_run(&ctx->pool, ctx->root, vv_rect(0, 0, ctx->win_w, ctx->win_h));

    // --- Present phase (§4.1 steps 6-8) ---
    vv_present(ctx);

    ctx->tree_dirty = false;
    ctx->last_tier  = VV_TIER_BUILD;
    return &ctx->cmds;
}
