#include "verve/verve.h"
#include "vv_test.h"

// Golden layout checks against known geometry (§19). We read layout_rect (what
// layout decided) directly off nodes after end_frame.

static vv_Rect R(vv_Ctx *c, uint32_t idx) { return vv_node(c, idx)->layout_rect; }

static void run_tests(void) {
    vv_Ctx ctx;
    vv_init(&ctx);
    vv_set_window(&ctx, 400, 300, 1.0f);
    vv_Input in = {0};
    vv_Style s = {0};

    // --- Column, padding 10, gap 8, two fixed children ---
    uint32_t a = 0, b = 0;
    vv_begin_frame(&ctx, 0.016f, &in);
    VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .padding = vv_all(10), .gap = 8,
                                   .w = vv_fixed(200), .h = vv_fixed(200) }), s) {
        a = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(50), .h = vv_fixed(30) }, s);
        vv_end_box(&ctx);
        b = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(50), .h = vv_fixed(40) }, s);
        vv_end_box(&ctx);
    }
    vv_end_frame(&ctx);
    // First child at (pad.l, pad.t) = (10,10); second below by 30 + gap 8.
    CHECK_NEAR(R(&ctx, a).x, 10, 0.01); CHECK_NEAR(R(&ctx, a).y, 10, 0.01);
    CHECK_NEAR(R(&ctx, b).y, 48, 0.01);              // 10 + 30 + 8
    CHECK_NEAR(R(&ctx, b).h, 40, 0.01);

    // --- Row with two GROW children splitting leftover by weight ---
    uint32_t g1 = 0, g2 = 0;
    vv_begin_frame(&ctx, 0.016f, &in);
    VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 10,
                                   .w = vv_fixed(310), .h = vv_fixed(50) }), s) {
        g1 = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_fixed(50) }, s);
        vv_end_box(&ctx);
        g2 = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_grow(2), .h = vv_fixed(50) }, s);
        vv_end_box(&ctx);
    }
    vv_end_frame(&ctx);
    // content 310, gap 10 => 300 to split 1:2 => 100 and 200.
    CHECK_NEAR(R(&ctx, g1).w, 100, 0.01);
    CHECK_NEAR(R(&ctx, g2).w, 200, 0.01);
    CHECK_NEAR(R(&ctx, g2).x, 110, 0.01);            // 100 + gap 10

    // --- PERCENT + cross-axis CENTER alignment ---
    uint32_t p = 0;
    vv_begin_frame(&ctx, 0.016f, &in);
    VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .cross = VV_ALIGN_CENTER,
                                   .w = vv_fixed(200), .h = vv_fixed(100) }), s) {
        p = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_percent(0.5f), .h = vv_fixed(20) }, s);
        vv_end_box(&ctx);
    }
    vv_end_frame(&ctx);
    CHECK_NEAR(R(&ctx, p).w, 100, 0.01);             // 50% of 200
    CHECK_NEAR(R(&ctx, p).y, 40, 0.01);              // centered: (100-20)/2

    // --- FIT container hugs its fixed children ---
    uint32_t fitc = 0;
    vv_begin_frame(&ctx, 0.016f, &in);
    fitc = vv_box(&ctx, (vv_LayoutDecl){ .dir = VV_ROW, .gap = 5,
                                         .padding = vv_all(4) }, s);
    {
        vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(30), .h = vv_fixed(30) }, s);
        vv_end_box(&ctx);
        vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(70), .h = vv_fixed(10) }, s);
        vv_end_box(&ctx);
    }
    vv_end_box(&ctx);
    vv_end_frame(&ctx);
    // width = pad(8) + 30 + gap(5) + 70 = 113; height = pad(8) + max(30,10) = 38.
    CHECK_NEAR(R(&ctx, fitc).w, 113, 0.01);
    CHECK_NEAR(R(&ctx, fitc).h, 38, 0.01);

    vv_shutdown(&ctx);
}

TEST_MAIN()
