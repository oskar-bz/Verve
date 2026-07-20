#include "verve/verve.h"
#include "vv_test.h"

// The vector draw-list (§14.5): vv_draw_* builders attach polylines/polygons to
// a node; present lowers them to VV_CMD_POLY translated to window space.

static uint32_t g_box;

// A view that draws a 3-point polyline and 2 dots inside a positioned box.
static void draw_view(vv_Ctx *c, void *st) {
    (void)st;
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                               .padding = vv_all(40)}),
           ((vv_Style){0})) {
        g_box = vv_box_keyed(c, "canvas", 6,
                             (vv_LayoutDecl){.w = vv_fixed(200), .h = vv_fixed(100),
                                             .clip = true},
                             (vv_Style){.bg = vv_rgb(0.1f, 0.1f, 0.1f)});
        vv_Vec2 line[3] = {{0, 0}, {100, 50}, {200, 0}}; // local coords
        vv_draw_polyline(c, g_box, line, 3, 2.0f, vv_rgb(1, 0, 0));
        vv_Vec2 dots[2] = {{10, 90}, {190, 90}};
        vv_draw_points(c, g_box, dots, 2, 4.0f, vv_rgb(0, 1, 0));
        vv_end_box(c);
    }
}

int main(void) {
    // vv_remapf sanity.
    CHECK_NEAR(vv_remapf(5, 0, 10, 0, 100), 50.0f, 1e-4f);
    CHECK_NEAR(vv_remapf(0, 0, 10, 20, 40), 20.0f, 1e-4f);
    CHECK_NEAR(vv_remapf(1, 1, 1, 7, 9), 7.0f, 1e-4f); // zero-width range -> out_lo

    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 300, 1.0f);
    vv_set_animation_scale(&ctx, 0.0f); // snap: no enter-scale skewing the origin
    vv_Input in = {0};
    vv_begin_frame(&ctx, 0.016f, &in); draw_view(&ctx, NULL); vv_end_frame(&ctx);
    vv_begin_frame(&ctx, 0.016f, &in); draw_view(&ctx, NULL);
    vv_CommandBuffer *cb = vv_end_frame(&ctx);

    // The box sits at padding (40,40); its content polys must be translated there.
    vv_Rect box = vv_pool_get(&ctx.pool, g_box)->actual_rect;
    CHECK_NEAR(box.x, 40.0f, 0.5f);
    CHECK_NEAR(box.y, 40.0f, 0.5f);

    // Count polys and locate the stroked polyline + the points.
    int npoly = 0, nstroke = 0, npoints = 0;
    const vv_CmdPoly *stroke = NULL, *points = NULL;
    for (uint32_t i = 0; i < cb->count; i++) {
        if (cb->items[i].kind != VV_CMD_POLY) continue;
        const vv_CmdPoly *p = &cb->items[i].as.poly;
        npoly++;
        if (p->flags & VV_POLY_POINTS) { npoints++; points = p; }
        else { nstroke++; stroke = p; }
    }
    CHECK(npoly == 2);
    CHECK(nstroke == 1);
    CHECK(npoints == 1);

    // Stroke: 3 local points, origin at the box, width preserved.
    CHECK(stroke && stroke->count == 3);
    CHECK_NEAR(stroke->width, 2.0f, 1e-4f);
    CHECK_NEAR(stroke->origin.x, box.x, 0.5f);
    CHECK_NEAR(stroke->origin.y, box.y, 0.5f);
    // Points stay LOCAL in the command; origin carries the translation.
    CHECK_NEAR(stroke->pts[1].x, 100.0f, 0.5f);
    CHECK_NEAR(stroke->pts[1].y, 50.0f, 0.5f);
    // So the window-space apex is origin + local.
    CHECK_NEAR(stroke->origin.x + stroke->pts[1].x, 140.0f, 0.5f);

    // Points: radius 4 encoded as width 8 (diameter), 2 vertices.
    CHECK(points && points->count == 2);
    CHECK_NEAR(points->width, 8.0f, 1e-4f);

    vv_shutdown(&ctx);
    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n");
    return 0;
}
