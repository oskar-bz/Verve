#include "verve/verve.h"
#include "vv_test.h"
#include <stdio.h>

// Reproduce: press on a slider track and drag; the returned value must track
// the pointer x. If it stays put, active/geometry routing is broken.

static float build(vv_Ctx *ctx, float v, vv_Vec2 mouse, bool down, uint32_t *out_track) {
    vv_Input in = { .mouse = mouse, .mouse_down = down };
    vv_begin_frame(ctx, 0.016f, &in);
    float out = v;
    // FIT column + FIT row: the pathological container that used to collapse
    // the track to width 0 (label + slider in a bare row, as in the demo).
    VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .padding = vv_all(10) }),
           (vv_Style){0}) {
        VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 10 }), (vv_Style){0}) {
            vv_label(ctx, "Duration:");
            out = vv_slider(ctx, "s", v, 0, 1);
        }
    }
    vv_end_frame(ctx);
    (void)out_track;
    return out;
}

static void run_tests(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 400, 1.0f);
    ctx.animation_scale = 0.0f; // snap, deterministic geometry

    float v = 0.5f;
    // Settle geometry.
    for (int i = 0; i < 3; i++) v = build(&ctx, v, vv_v2(-1, -1), false, NULL);

    // The track is >=160 wide via its grow-min (starts after "Duration:" label).
    vv_Vec2 press = { 110, 24 }, drag = { 230, 24 };
    v = build(&ctx, v, press, true, NULL);
    printf("after press at x=%.0f: v=%.3f\n", press.x, v);
    v = build(&ctx, v, drag, true, NULL);
    printf("after drag to x=%.0f: v=%.3f\n", drag.x, v);
    CHECK(v > 0.5f); // moved right of where it started

    vv_shutdown(&ctx);
}

TEST_MAIN()
