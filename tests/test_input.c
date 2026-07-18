#include "verve/verve.h"
#include "vv_test.h"

// Two side-by-side buttons; drive synthetic pointer input and assert the
// hover/press/click/capture semantics (§11).

static uint32_t g_a, g_b;

static void build(vv_Ctx *ctx) {
    vv_Style s = { .bg = vv_rgb(0.3f,0.3f,0.3f) };
    VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 20, .padding = vv_all(10) }), s) {
        g_a = vv_box_keyed(ctx, "a", 1,
            (vv_LayoutDecl){ .w = vv_fixed(80), .h = vv_fixed(40), .focusable = true }, s);
        vv_end_box(ctx);
        g_b = vv_box_keyed(ctx, "b", 1,
            (vv_LayoutDecl){ .w = vv_fixed(80), .h = vv_fixed(40) }, s);
        vv_end_box(ctx);
    }
}

static vv_CommandBuffer *frame(vv_Ctx *ctx, float mx, float my, bool down, float wheel) {
    vv_Input in = { .mouse = vv_v2(mx, my), .mouse_down = down, .wheel = wheel };
    vv_begin_frame(ctx, 0.016f, &in);
    build(ctx);
    return vv_end_frame(ctx);
}

static void run_tests(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 300, 200, 1.0f);

    // Button A occupies roughly (10,10)-(90,50); B (110,10)-(190,50).
    // Frame 1 establishes geometry (hit test uses last frame's rects).
    frame(&ctx, 0, 0, false, 0);

    // Hover A.
    frame(&ctx, 40, 30, false, 0);
    CHECK(vv_hovered(&ctx, g_a));
    CHECK(!vv_hovered(&ctx, g_b));

    // Press on A: pressed + active(capture) + focus (A is focusable).
    frame(&ctx, 40, 30, true, 0);
    CHECK(vv_pressed(&ctx, g_a));
    CHECK(vv_active(&ctx, g_a));
    CHECK(vv_focused(&ctx, g_a));
    CHECK(!vv_clicked(&ctx, g_a)); // not yet released

    // Drag far away while held: capture keeps A active; drag delta tracks.
    frame(&ctx, 250, 180, true, 0);
    CHECK(vv_active(&ctx, g_a));            // survived pointer leaving (§11.2)
    vv_Vec2 d = vv_drag_delta(&ctx, g_a);
    CHECK_NEAR(d.x, 210, 0.5); CHECK_NEAR(d.y, 150, 0.5);

    // Release outside A: no click (release not on captured node).
    frame(&ctx, 250, 180, false, 0);
    CHECK(!vv_clicked(&ctx, g_a));
    CHECK(!vv_active(&ctx, g_a));

    // Full press+release inside B: click fires. B is not focusable, so pressing
    // it clears focus from A.
    frame(&ctx, 150, 30, true, 0);
    CHECK(vv_active(&ctx, g_b));
    CHECK(!vv_focused(&ctx, g_a));
    frame(&ctx, 150, 30, false, 0);
    CHECK(vv_clicked(&ctx, g_b));

    // Disabled node is not hittable.
    {
        vv_Input in = { .mouse = vv_v2(40, 30) };
        vv_begin_frame(&ctx, 0.016f, &in);
        vv_Style s = {0};
        VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .padding = vv_all(10) }), s) {
            uint32_t d2 = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(80), .h = vv_fixed(40),
                                                        .disabled = true }, s);
            vv_end_box(&ctx);
            (void)d2;
        }
        vv_end_frame(&ctx);
        // Re-run so hit test sees the disabled geometry.
        vv_begin_frame(&ctx, 0.016f, &in);
        uint32_t dd;
        VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .padding = vv_all(10) }), s) {
            dd = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(80), .h = vv_fixed(40),
                                              .disabled = true }, s);
            vv_end_box(&ctx);
        }
        vv_end_frame(&ctx);
        CHECK(!vv_hovered(&ctx, dd));
    }

    vv_shutdown(&ctx);
}

TEST_MAIN()
