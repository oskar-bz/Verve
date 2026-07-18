#include "verve/verve.h"
#include "vv_test.h"

// Full idle mode (§4.2, Phase 10): once animations settle and there's no input,
// vv_run_frame skips the present pass and returns NULL so the caller can avoid
// drawing. Input or animation brings it back.

static void update_fn(void *s, vv_Event e) { (void)s; (void)e; }
static void view_fn(vv_Ctx *ctx, void *s) {
    (void)s;
    VV_BOX(ctx, ((vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1) }),
           (vv_Style){ .bg = vv_rgb(0.1f, 0.1f, 0.1f) }) {
        vv_text(ctx, "static", (vv_Style){ .font_size = 14 });
    }
}

static void run_tests(void) {
    // Idle mode ON: a static UI idles (NULL) after it settles.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        vv_set_idle_mode(&ctx, true);
        vv_set_animation_scale(&ctx, 0.0f); // snap springs so it settles at once

        vv_CommandBuffer *c1 = vv_run_frame(&ctx, 0.016f, &(vv_Input){0}, update_fn, view_fn, NULL);
        CHECK(c1 != NULL);                       // first frame builds + draws
        CHECK(ctx.last_tier == VV_TIER_BUILD);

        vv_CommandBuffer *c2 = vv_run_frame(&ctx, 0.016f, &(vv_Input){0}, update_fn, view_fn, NULL);
        CHECK(c2 == NULL);                       // settled + no input -> idle
        CHECK(ctx.last_tier == VV_TIER_IDLE);

        // Moving the mouse wakes it (a build to refresh hover/geometry).
        vv_CommandBuffer *c3 = vv_run_frame(&ctx, 0.016f, &(vv_Input){ .mouse = vv_v2(20, 20) },
                                            update_fn, view_fn, NULL);
        CHECK(c3 != NULL);
        CHECK(ctx.last_tier == VV_TIER_BUILD);
        vv_shutdown(&ctx);
    }

    // Idle mode OFF: always returns a buffer (present every frame), never idles.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        vv_set_animation_scale(&ctx, 0.0f);
        vv_run_frame(&ctx, 0.016f, &(vv_Input){0}, update_fn, view_fn, NULL);
        vv_CommandBuffer *c = vv_run_frame(&ctx, 0.016f, &(vv_Input){0}, update_fn, view_fn, NULL);
        CHECK(c != NULL);
        CHECK(ctx.last_tier == VV_TIER_PRESENT);
        vv_shutdown(&ctx);
    }
}

TEST_MAIN()
