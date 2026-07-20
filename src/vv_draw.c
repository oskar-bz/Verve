// vv_draw.c — the vv_draw_* canvas builders (see vv_draw.h).
//
// Each builder allocates a vv_Poly (and a copy of its points) in the frame arena
// and links it onto the target node's draw-list. Present walks that list and
// lowers each poly to a VV_CMD_POLY. Points are stored in the node's LOCAL space;
// present translates them to window space when it lowers them.
#include "verve/vv_draw.h"

#include "verve/vv_context.h"
#include "verve/vv_node.h"

#include <string.h>

// Grab (or lazily create) the node's draw-list, then append `p`.
static void append(vv_Ctx *ctx, uint32_t node, vv_Poly *p) {
    vv_Node *n = vv_pool_get(&ctx->pool, node);
    if (!n) return;
    vv_DrawList *dl = (vv_DrawList *)n->draw;
    if (!dl) {
        dl = vv_arena_alloc(&ctx->frame, sizeof *dl);
        dl->head = dl->tail = NULL;
        n->draw = dl;
    }
    p->next = NULL;
    if (dl->tail) dl->tail->next = p;
    else dl->head = p;
    dl->tail = p;
}

// Build a poly from a copy of `pts` (may be NULL to fill later).
static vv_Poly *make(vv_Ctx *ctx, const vv_Vec2 *pts, int n, float width,
                     vv_Color color, uint8_t flags) {
    if (n < 0) n = 0;
    vv_Poly *p = vv_arena_alloc(&ctx->frame, sizeof *p);
    p->count = (uint32_t)n;
    p->width = width;
    p->color = color;
    p->flags = flags;
    p->next = NULL;
    if (n > 0) {
        p->pts = vv_arena_alloc(&ctx->frame, (size_t)n * sizeof(vv_Vec2));
        if (pts) memcpy(p->pts, pts, (size_t)n * sizeof(vv_Vec2));
    } else {
        p->pts = NULL;
    }
    return p;
}

void vv_draw_polyline(vv_Ctx *ctx, uint32_t node, const vv_Vec2 *pts, int n,
                      float width, vv_Color color) {
    if (n < 2) return;
    append(ctx, node, make(ctx, pts, n, width, color, 0));
}

void vv_draw_line(vv_Ctx *ctx, uint32_t node, vv_Vec2 a, vv_Vec2 b,
                  float width, vv_Color color) {
    vv_Poly *p = make(ctx, NULL, 2, width, color, 0);
    p->pts[0] = a;
    p->pts[1] = b;
    append(ctx, node, p);
}

void vv_draw_polygon(vv_Ctx *ctx, uint32_t node, const vv_Vec2 *pts, int n,
                     vv_Color fill) {
    if (n < 3) return;
    append(ctx, node, make(ctx, pts, n, 0.0f, fill, VV_POLY_FILL | VV_POLY_CLOSED));
}

void vv_draw_points(vv_Ctx *ctx, uint32_t node, const vv_Vec2 *pts, int n,
                    float r, vv_Color color) {
    if (n < 1 || r <= 0.0f) return;
    // The backend draws a disc of radius = width/2 at each vertex.
    append(ctx, node, make(ctx, pts, n, r * 2.0f, color, VV_POLY_POINTS));
}
