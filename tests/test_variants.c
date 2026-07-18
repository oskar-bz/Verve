#include "verve/verve.h"
#include "vv_test.h"

// Declarative variants (§4.4/§7.1): hovering a node must retarget its style so
// the springs animate toward the variant. Resolution happens at build time from
// the interaction flags set by the input pass.

static void run_tests(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);

    vv_Color base = vv_rgb(0.2f, 0.2f, 0.2f);
    vv_Color hovc = vv_rgb(0.8f, 0.4f, 0.1f);
    vv_Style hov = { .bg = hovc };
    uint32_t btn = 0;

    // Frame 1: establish geometry, pointer away.
    vv_Input in = { .mouse = vv_v2(-10, -10) };
    vv_begin_frame(&ctx, 0.016f, &in);
    btn = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(100), .h = vv_fixed(40) },
                 (vv_Style){ .bg = base, .hover = &hov });
    vv_end_box(&ctx);
    vv_end_frame(&ctx);
    // Not hovered: target bg is the base.
    CHECK_NEAR(vv_node(&ctx, btn)->target.bg.r, base.r, 0.001);

    // Frame 2: pointer over the button. Input runs at begin_frame against last
    // frame's geometry, so the HOVERED flag is set before build resolves it.
    in.mouse = vv_v2(50, 20);
    vv_begin_frame(&ctx, 0.016f, &in);
    btn = vv_box(&ctx, (vv_LayoutDecl){ .w = vv_fixed(100), .h = vv_fixed(40) },
                 (vv_Style){ .bg = base, .hover = &hov });
    vv_end_box(&ctx);
    vv_end_frame(&ctx);
    CHECK(vv_hovered(&ctx, btn));
    // Target bg now the hover color (variant folded in).
    CHECK_NEAR(vv_node(&ctx, btn)->target.bg.r, hovc.r, 0.001);
    CHECK_NEAR(vv_node(&ctx, btn)->target.bg.g, hovc.g, 0.001);

    // The actual (animated) bg should be moving toward hover but not there yet
    // after one frame — proof it springs rather than snaps.
    vv_Color actual = vv_oklab_to_srgb((vv_Oklab){
        vv_node(&ctx, btn)->actual.bg[0].x, vv_node(&ctx, btn)->actual.bg[1].x,
        vv_node(&ctx, btn)->actual.bg[2].x, vv_node(&ctx, btn)->actual.bg[3].x });
    CHECK(actual.r < hovc.r); // still catching up

    vv_shutdown(&ctx);
}

TEST_MAIN()
