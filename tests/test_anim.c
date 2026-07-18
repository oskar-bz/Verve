#include "verve/verve.h"
#include "vv_test.h"
#include <math.h>

static void test_spring(void) {
    vv_Spring s;
    vv_spring_init(&s, 0.0f, VV_SMOOTH);
    CHECK(s.settled);
    vv_spring_retarget(&s, 100.0f);
    CHECK(!s.settled);
    // Critically damped: converges monotonically, no overshoot past target.
    float maxv = 0;
    for (int i = 0; i < 600 && !s.settled; i++) {
        vv_spring_step(&s, 1.0f / 60.0f);
        maxv = vv_maxf(maxv, s.x);
    }
    CHECK(s.settled);
    CHECK_NEAR(s.x, 100.0f, 0.1);
    CHECK(maxv <= 100.5f); // no meaningful overshoot when damping=1

    // Retarget mid-flight carries velocity (graceful interrupt).
    vv_spring_init(&s, 0, VV_SMOOTH);
    vv_spring_retarget(&s, 100);
    for (int i = 0; i < 5; i++) vv_spring_step(&s, 1.0f / 60.0f);
    CHECK(s.v > 0);
    vv_spring_retarget(&s, 0); // reverse
    CHECK(!s.settled);
}

static void test_oklab(void) {
    // Round-trip fidelity.
    vv_Color c = vv_rgb(0.2f, 0.55f, 0.95f);
    vv_Color rt = vv_oklab_to_srgb(vv_srgb_to_oklab(c));
    CHECK_NEAR(rt.r, c.r, 0.002);
    CHECK_NEAR(rt.g, c.g, 0.002);
    CHECK_NEAR(rt.b, c.b, 0.002);

    // Endpoints exact.
    vv_Color mid0 = vv_color_lerp(vv_rgb(1,0,0), vv_rgb(1,1,0), 0.0f);
    vv_Color mid1 = vv_color_lerp(vv_rgb(1,0,0), vv_rgb(1,1,0), 1.0f);
    CHECK_NEAR(mid0.r, 1.0, 0.005); CHECK_NEAR(mid0.g, 0.0, 0.01);
    CHECK_NEAR(mid1.g, 1.0, 0.005);

    // Blue->yellow midpoint must NOT be grey mud: it should stay reasonably
    // saturated (the whole point of Oklab, §6.3). Grey => r~g~b.
    vv_Color m = vv_color_lerp(vv_rgb(0,0,1), vv_rgb(1,1,0), 0.5f);
    float spread = vv_maxf(vv_maxf(m.r, m.g), m.b) - vv_minf(vv_minf(m.r, m.g), m.b);
    CHECK(spread > 0.2f);
}

static void test_flip_and_lifecycle(void) {
    vv_Ctx ctx;
    vv_init(&ctx);
    vv_set_window(&ctx, 400, 300, 1.0f);
    vv_Input in = {0};
    vv_Style s = { .bg = vv_rgb(0.2f, 0.2f, 0.2f) };

    // Frame 1: a fixed box at some position. On birth actual snaps to layout.
    uint32_t box = 0;
    vv_begin_frame(&ctx, 0.016f, &in);
    VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .padding = vv_all(10) }), s) {
        box = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(50), .h = vv_fixed(50) }, s);
        vv_end_box(&ctx);
    }
    vv_end_frame(&ctx);
    vv_Rect r0 = vv_node(&ctx, box)->actual_rect;
    CHECK_NEAR(r0.y, 10, 0.5); // snapped to layout on birth, no enter offset in rect

    // Now push it down by growing the padding: layout jumps, actual should
    // lag behind (FLIP), not teleport.
    vv_begin_frame(&ctx, 0.016f, &in);
    VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .padding = (vv_Edges){10,120,10,10} }), s) {
        box = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(50), .h = vv_fixed(50) }, s);
        vv_end_box(&ctx);
    }
    vv_end_frame(&ctx);
    vv_Rect r1 = vv_node(&ctx, box)->actual_rect;
    CHECK(vv_node(&ctx, box)->layout_rect.y > 100);   // layout jumped
    CHECK(r1.y < 100);                                 // actual is still catching up
    CHECK(ctx.unsettled_springs > 0);

    // Run frames until settled: actual converges on layout.
    for (int i = 0; i < 400; i++) {
        vv_begin_frame(&ctx, 0.016f, &in);
        VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .padding = (vv_Edges){10,120,10,10} }), s) {
            vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(50), .h = vv_fixed(50) }, s);
            vv_end_box(&ctx);
        }
        vv_end_frame(&ctx);
    }
    vv_Node *bn = vv_node(&ctx, box);
    CHECK_NEAR(bn->actual_rect.y, bn->layout_rect.y, 0.5);

    // animation_scale = 0 snaps instantly (deterministic mode, §18/§19).
    vv_set_animation_scale(&ctx, 0.0f);
    vv_begin_frame(&ctx, 0.016f, &in);
    VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .padding = vv_all(5) }), s) {
        box = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(50), .h = vv_fixed(50) }, s);
        vv_end_box(&ctx);
    }
    vv_end_frame(&ctx);
    bn = vv_node(&ctx, box);
    CHECK_NEAR(bn->actual_rect.y, bn->layout_rect.y, 0.01); // snapped
    CHECK_EQ_U(ctx.unsettled_springs, 0);

    vv_shutdown(&ctx);
}

static void run_tests(void) {
    test_spring();
    test_oklab();
    test_flip_and_lifecycle();
}

TEST_MAIN()
