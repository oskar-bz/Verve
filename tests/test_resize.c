#include "verve/verve.h"
#include "vv_test.h"

// Regression (§4.5): widgets position children by reading a parent's *animated*
// actual_rect at build time — the slider fill, the tab indicator, popover
// anchors. actual_rect is updated in present, one frame after the build that
// read it, so on a window resize (which relayouts with no input) those children
// would freeze at the pre-resize geometry until some stray hover forced a
// rebuild. A moving rect spring — including a teleport snap — now marks the tree
// for a rebuild so the dependents catch up on their own.

static uint32_t g_ind;

static void update_fn(void *s, vv_Event e) { (void)s; (void)e; }

// A full-width bar with an "indicator" child placed at half the bar's *actual*
// width — the same actual_rect-at-build-time read the real widgets do.
static void view_fn(vv_Ctx *ctx, void *s) {
    (void)s;
    uint32_t bar = vv_box_keyed(ctx, "bar", 3,
        (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_fixed(40) },
        (vv_Style){ .bg = vv_rgb(0.2f, 0.2f, 0.2f) });
    float bw = vv_node(ctx, bar)->actual_rect.w;
    g_ind = vv_box_keyed(ctx, "ind", 3,
        (vv_LayoutDecl){ .has_absolute = true, .absolute = vv_rect(bw * 0.5f, 0, 20, 40) },
        (vv_Style){ .bg = vv_rgb(0.8f, 0.6f, 0.2f) });
    vv_end_box(ctx);
    vv_end_box(ctx);
}

static vv_CommandBuffer *frame(vv_Ctx *ctx) {
    // Constant input every frame: nothing here should wake a rebuild on its own.
    return vv_run_frame(ctx, 0.016f, &(vv_Input){ .mouse = vv_v2(5, 5) },
                        update_fn, view_fn, NULL);
}

static void run_tests(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 40, 1.0f);
    vv_set_idle_mode(&ctx, true);
    vv_set_animation_scale(&ctx, 0.0f); // snap: exercises the teleport path

    // Settle at width 200: indicator ends up at half-width (~100).
    for (int i = 0; i < 6; i++) frame(&ctx);
    CHECK(frame(&ctx) == NULL);                  // fully settled -> idle
    CHECK(fabsf(vv_node(&ctx, g_ind)->layout_rect.x - 100.0f) < 1.0f);

    // Resize to 400 with NO input change. The bar teleports to the new width;
    // the indicator must follow to ~200 within a couple of self-driven builds.
    vv_set_window(&ctx, 400, 40, 1.0f);
    CHECK(frame(&ctx) != NULL);                  // resize forces a build
    CHECK(ctx.last_tier == VV_TIER_BUILD);
    frame(&ctx);                                 // dependents catch up
    frame(&ctx);

    CHECK(fabsf(vv_node(&ctx, g_ind)->layout_rect.x - 200.0f) < 1.0f);

    // And once caught up, it returns to idle rather than rebuilding forever.
    CHECK(frame(&ctx) == NULL);
    CHECK(ctx.last_tier == VV_TIER_IDLE);

    vv_shutdown(&ctx);
}

TEST_MAIN()
