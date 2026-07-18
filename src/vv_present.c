#include "verve/vv_color.h"
#include "verve/vv_context.h"
#include "verve/vv_internal.h"

#include <math.h>
#include <string.h>

// The Present phase (§4.1 steps 6-8, §6). Springs live on the retained tree and
// advance here without user code: a hover transition, a FLIP reflow, an
// enter/exit — none need a rebuild. Everything below reads `target` + `decl`
// (set during Build) and writes `actual` + emits commands.

// A layout delta larger than this fraction of the window snaps instead of
// flying across (§6.5).
#define FLIP_SNAP_FRAC 0.9f

static vv_SpringParams node_params(const vv_Node *n) {
    if (n->target.spring.response > 0.0001f) return n->target.spring;
    return VV_DEFAULT_SPRING;
}

// dt after the global animation-scale kill switch (§18). <=0 => snap.
static float scaled_dt(const vv_Ctx *ctx) {
    return ctx->animation_scale <= 0.0f ? -1.0f : ctx->dt * ctx->animation_scale;
}

static void step1(vv_Ctx *ctx, vv_Spring *s, float dt) {
    if (dt < 0.0f) { vv_spring_snap(s); return; }
    vv_spring_step(s, dt);
    if (!s->settled) ctx->unsettled_springs++;
}

// ---- color springs (held in Oklab; §6.3) ---------------------------------

static void color_init(vv_Spring sp[4], vv_Color c, vv_SpringParams p) {
    vv_Oklab o = vv_srgb_to_oklab(c);
    vv_spring_init(&sp[0], o.L, p);
    vv_spring_init(&sp[1], o.a, p);
    vv_spring_init(&sp[2], o.b, p);
    vv_spring_init(&sp[3], o.alpha, p);
}
static void color_retarget(vv_Spring sp[4], vv_Color c) {
    vv_Oklab o = vv_srgb_to_oklab(c);
    vv_spring_retarget(&sp[0], o.L);
    vv_spring_retarget(&sp[1], o.a);
    vv_spring_retarget(&sp[2], o.b);
    vv_spring_retarget(&sp[3], o.alpha);
}
static void color_step(vv_Ctx *ctx, vv_Spring sp[4], float dt) {
    for (int i = 0; i < 4; i++) step1(ctx, &sp[i], dt);
}
static vv_Color color_read(const vv_Spring sp[4]) {
    return vv_oklab_to_srgb((vv_Oklab){ sp[0].x, sp[1].x, sp[2].x, sp[3].x });
}

// ---- scalar spring vectors -----------------------------------------------

static void scalars_init(vv_Spring *sp, const float *v, int n, vv_SpringParams p) {
    for (int i = 0; i < n; i++) vv_spring_init(&sp[i], v[i], p);
}
static void scalars_retarget(vv_Spring *sp, const float *v, int n) {
    for (int i = 0; i < n; i++) vv_spring_retarget(&sp[i], v[i]);
}
static void scalars_step(vv_Ctx *ctx, vv_Spring *sp, int n, float dt) {
    for (int i = 0; i < n; i++) step1(ctx, &sp[i], dt);
}

// ---- transform decomposition ---------------------------------------------

static float xform_scale(vv_Mat23 m) { return sqrtf(m.a * m.a + m.b * m.b); }
static float xform_rot(vv_Mat23 m)   { return atan2f(m.b, m.a); }

// ---- style animation ------------------------------------------------------

static void style_init(vv_Node *n) {
    vv_StyleAnim *A = &n->actual;
    vv_Style *T = &n->target;
    vv_SpringParams p = node_params(n);

    color_init(A->bg, T->bg, p);
    color_init(A->fg, T->fg, p);
    color_init(A->border_color, T->border_color, p);
    color_init(A->shadow_color, T->shadow.color, p);

    float rad[4] = { T->radius.tl, T->radius.tr, T->radius.br, T->radius.bl };
    float bw[4]  = { T->border_width.l, T->border_width.t, T->border_width.r, T->border_width.b };
    scalars_init(A->radius, rad, 4, p);
    scalars_init(A->border_width, bw, 4, p);

    float op = T->opacity > 0.0f ? T->opacity : 1.0f;
    scalars_init(&A->opacity, &op, 1, p);
    float sc = xform_scale(T->transform); if (sc <= 0.0f) sc = 1.0f;
    scalars_init(&A->scale, &sc, 1, p);
    float ro = xform_rot(T->transform);
    scalars_init(&A->rotation, &ro, 1, p);

    A->initialized = true;
}

static void style_retarget(vv_Node *n) {
    vv_StyleAnim *A = &n->actual;
    vv_Style *T = &n->target;
    color_retarget(A->bg, T->bg);
    color_retarget(A->fg, T->fg);
    color_retarget(A->border_color, T->border_color);
    color_retarget(A->shadow_color, T->shadow.color);
    float rad[4] = { T->radius.tl, T->radius.tr, T->radius.br, T->radius.bl };
    float bw[4]  = { T->border_width.l, T->border_width.t, T->border_width.r, T->border_width.b };
    scalars_retarget(A->radius, rad, 4);
    scalars_retarget(A->border_width, bw, 4);
    float op = T->opacity > 0.0f ? T->opacity : 1.0f;
    scalars_retarget(&A->opacity, &op, 1);
    float sc = xform_scale(T->transform); if (sc <= 0.0f) sc = 1.0f;
    scalars_retarget(&A->scale, &sc, 1);
    float ro = xform_rot(T->transform);
    scalars_retarget(&A->rotation, &ro, 1);
}

static void style_step(vv_Ctx *ctx, vv_Node *n, float dt) {
    vv_StyleAnim *A = &n->actual;
    color_step(ctx, A->bg, dt);
    color_step(ctx, A->fg, dt);
    color_step(ctx, A->border_color, dt);
    color_step(ctx, A->shadow_color, dt);
    scalars_step(ctx, A->radius, 4, dt);
    scalars_step(ctx, A->border_width, 4, dt);
    scalars_step(ctx, &A->opacity, 1, dt);
    scalars_step(ctx, &A->scale, 1, dt);
    scalars_step(ctx, &A->rotation, 1, dt);
}

// ---- FLIP rect springs (§6.5) --------------------------------------------

static void rect_init(vv_Node *n, vv_SpringParams p) {
    vv_spring_init(&n->rx, n->layout_rect.x, p);
    vv_spring_init(&n->ry, n->layout_rect.y, p);
    vv_spring_init(&n->rw, n->layout_rect.w, p);
    vv_spring_init(&n->rh, n->layout_rect.h, p);
}

static void rect_animate(vv_Ctx *ctx, vv_Node *n, float dt) {
    float snap_dist = FLIP_SNAP_FRAC * vv_maxf(ctx->win_w, ctx->win_h);
    float dx = n->layout_rect.x - n->rx.x, dy = n->layout_rect.y - n->ry.x;
    bool teleport = (fabsf(dx) > snap_dist) || (fabsf(dy) > snap_dist);

    vv_spring_retarget(&n->rx, n->layout_rect.x);
    vv_spring_retarget(&n->ry, n->layout_rect.y);
    vv_spring_retarget(&n->rw, n->layout_rect.w);
    vv_spring_retarget(&n->rh, n->layout_rect.h);
    if (teleport) {
        vv_spring_snap(&n->rx); vv_spring_snap(&n->ry);
        vv_spring_snap(&n->rw); vv_spring_snap(&n->rh);
    } else {
        step1(ctx, &n->rx, dt); step1(ctx, &n->ry, dt);
        step1(ctx, &n->rw, dt); step1(ctx, &n->rh, dt);
    }
}

// ---- emit -----------------------------------------------------------------

static vv_Command *push_cmd(vv_Ctx *ctx) {
    vv_CommandBuffer *cb = &ctx->cmds;
    if (cb->count == cb->cap) {
        uint32_t ncap = cb->cap ? cb->cap * 2 : 256;
        vv_Command *ni = vv_arena_alloc(&ctx->frame, sizeof(vv_Command) * ncap);
        if (cb->count) memcpy(ni, cb->items, sizeof(vv_Command) * cb->count);
        cb->items = ni; cb->cap = ncap;
    }
    return &cb->items[cb->count++];
}

static vv_Color with_alpha(vv_Color c, float a) { c.a *= a; return c; }

static void emit_node(vv_Ctx *ctx, vv_Node *n, float inherited_opacity) {
    vv_StyleAnim *A = &n->actual;

    // Lifecycle factors from enter/exit springs (§6.6).
    float life_op = 1.0f, life_scale = 1.0f;
    if (n->flags & VV_FLAG_EXITING) {
        life_op = n->exit.x;                       // 1 -> 0
        life_scale = vv_lerpf(0.98f, 1.0f, n->exit.x);
    } else {
        life_op = n->enter.x;                      // 0 -> 1
        life_scale = vv_lerpf(0.96f, 1.0f, n->enter.x);
    }

    float opacity = A->opacity.x * life_op * inherited_opacity;
    float scale   = A->scale.x * life_scale;

    // Apply uniform scale about the rect center (rotation deferred to a
    // TRANSFORM command in a later phase).
    vv_Rect r = n->actual_rect;
    if (scale != 1.0f) {
        float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        r.w *= scale; r.h *= scale;
        r.x = cx - r.w * 0.5f; r.y = cy - r.h * 0.5f;
    }

    if (n->flags & VV_FLAG_TEXT) {
        vv_Command *cmd = push_cmd(ctx);
        cmd->kind = VV_CMD_TEXT;
        cmd->as.text = (vv_CmdText){
            .utf8   = (const char *)n->widget_state,
            .len    = n->widget_state_size ? n->widget_state_size - 1 : 0,
            .font   = n->target.font,
            .size   = n->target.font_size > 0 ? n->target.font_size : 14.0f,
            .color  = with_alpha(color_read(A->fg), opacity),
            .origin = vv_v2(r.x, r.y + (n->target.font_size > 0 ? n->target.font_size : 14.0f)),
        };
        return;
    }

    vv_Color bg = color_read(A->bg);
    // Skip fully-transparent fills with no border/shadow (the common leaf).
    bool has_border = (A->border_width[0].x + A->border_width[1].x +
                       A->border_width[2].x + A->border_width[3].x) > 0.01f;
    bool has_shadow = n->target.shadow.color.a > 0.001f;
    if (bg.a * opacity < 0.001f && !has_border && !has_shadow) return;

    vv_Command *cmd = push_cmd(ctx);
    cmd->kind = VV_CMD_RECT;
    cmd->as.rect = (vv_CmdRect){
        .rect   = r,
        .radius = (vv_Corners){ A->radius[0].x, A->radius[1].x, A->radius[2].x, A->radius[3].x },
        .fill_a = with_alpha(bg, opacity),
        .fill_b = with_alpha(bg, opacity),
        .border_width = (vv_Edges){ A->border_width[0].x, A->border_width[1].x,
                                    A->border_width[2].x, A->border_width[3].x },
        .border_color = with_alpha(color_read(A->border_color), opacity),
        .shadow = (vv_Shadow){
            .color  = with_alpha(color_read(A->shadow_color), opacity),
            .offset = n->target.shadow.offset,
            .blur   = n->target.shadow.blur,
            .spread = n->target.shadow.spread,
            .inset  = n->target.shadow.inset,
        },
    };
}

// ---- tree walk ------------------------------------------------------------

static void animate_and_emit(vv_Ctx *ctx, uint32_t index, float dt, float inh_op,
                             float scroll_ox, float scroll_oy) {
    vv_Node *n = vv_pool_get(&ctx->pool, index);
    vv_SpringParams p = node_params(n);

    // Birth: snap actual to target, no animation on first appearance of style
    // (§3.3, §6.6). Enter spring runs 0 -> 1.
    if (!n->actual.initialized) {
        style_init(n);
        rect_init(n, p);
        vv_spring_init(&n->enter, 0.0f, p);
        vv_spring_init(&n->exit, 1.0f, VV_DEFAULT_SPRING);
        vv_spring_init(&n->scroll_x, 0.0f, VV_SMOOTH);
        vv_spring_init(&n->scroll_y, 0.0f, VV_SMOOTH);
        vv_spring_retarget(&n->enter, 1.0f);
    } else {
        style_retarget(n);
        style_step(ctx, n, dt);
    }
    if (!(n->flags & VV_FLAG_EXITING)) step1(ctx, &n->enter, dt);

    // FLIP is scroll-free (chases layout_rect); scroll is a separate, snappy
    // offset baked into actual_rect so hit testing sees scrolled positions but
    // FLIP doesn't fight the continuous scroll motion (§6.4.1).
    rect_animate(ctx, n, dt);
    n->actual_rect = vv_rect(n->rx.x + scroll_ox, n->ry.x + scroll_oy, n->rw.x, n->rh.x);

    // Keep scroll offsets clamped as content size changes, then advance them.
    vv_spring_retarget(&n->scroll_x, vv_clampf(n->scroll_x.target, 0, n->scroll_max_x));
    vv_spring_retarget(&n->scroll_y, vv_clampf(n->scroll_y.target, 0, n->scroll_max_y));
    step1(ctx, &n->scroll_x, dt);
    step1(ctx, &n->scroll_y, dt);

    bool clips = n->decl.clip || n->decl.scroll_x || n->decl.scroll_y;
    if (clips) { push_cmd(ctx)->kind = VV_CMD_SCISSOR_PUSH; ctx->cmds.items[ctx->cmds.count-1].as.scissor = n->actual_rect; }

    emit_node(ctx, n, inh_op);

    // Descendants shift by this node's scroll offset (scrolling down moves
    // content up, hence subtraction).
    float child_ox = scroll_ox - n->scroll_x.x;
    float child_oy = scroll_oy - n->scroll_y.x;

    // Opacity inherits multiplicatively so a fading parent fades its subtree.
    float child_inh = inh_op * (n->flags & VV_FLAG_EXITING ? n->exit.x : n->enter.x)
                            * (n->actual.opacity.x > 0 ? n->actual.opacity.x : 1.0f);
    for (uint32_t c = n->first_child; c != VV_NIL;) {
        vv_Node *ch = vv_pool_get(&ctx->pool, c);
        uint32_t next = ch->next_sibling;
        animate_and_emit(ctx, c, dt, child_inh, child_ox, child_oy);
        c = next;
    }

    if (clips) push_cmd(ctx)->kind = VV_CMD_SCISSOR_POP;
}

// Detached exiting nodes (§3.3): drive their exit spring and paint the corpse
// on top using its last actual_rect. Text corpses are skipped (dangling string).
static void present_exiting(vv_Ctx *ctx, float dt) {
    vv_NodePool *pool = &ctx->pool;
    for (uint32_t i = 0; i < pool->count; i++) {
        vv_Node *n = &pool->nodes[i];
        if (!(n->flags & VV_FLAG_ALIVE) || !(n->flags & VV_FLAG_EXITING)) continue;
        if (n->last_touched_frame == ctx->frame_index) continue; // still in-tree
        step1(ctx, &n->exit, dt);
        if (n->flags & VV_FLAG_TEXT) continue;
        emit_node(ctx, n, 1.0f);
    }
}

void vv_present(vv_Ctx *ctx) {
    float dt = scaled_dt(ctx);
    animate_and_emit(ctx, ctx->root, dt, 1.0f, 0.0f, 0.0f);
    present_exiting(ctx, dt);
}
