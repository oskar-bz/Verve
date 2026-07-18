#include "verve/verve.h"
#include "vv_test.h"

// Full idle mode (§4.2, Phase 10): once animations settle and there's no input,
// vv_run_frame skips the present pass and returns NULL so the caller can avoid
// drawing. Crucially, plain mouse motion that doesn't change the hovered node
// does NOT wake a rebuild — only a hover/focus change, an edge, or input does.

static void update_fn(void *s, vv_Event e) { (void)s; (void)e; }

// Two side-by-side focusable boxes so we can move within one vs. cross between.
static void view_fn(vv_Ctx *ctx, void *s) {
    (void)s;
    VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1) }),
           (vv_Style){0}) {
        vv_box_keyed(ctx, "a", 1, (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1), .focusable = true },
                     (vv_Style){ .bg = vv_rgb(0.2f, 0.2f, 0.2f) });
        vv_end_box(ctx);
        vv_box_keyed(ctx, "b", 1, (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1), .focusable = true },
                     (vv_Style){ .bg = vv_rgb(0.3f, 0.3f, 0.3f) });
        vv_end_box(ctx);
    }
}

static vv_CommandBuffer *frame(vv_Ctx *ctx, vv_Vec2 m, bool down) {
    return vv_run_frame(ctx, 0.016f, &(vv_Input){ .mouse = m, .mouse_down = down },
                        update_fn, view_fn, NULL);
}

static void run_tests(void) {
    // Window 200x100: box "a" spans x[0,100), "b" spans x[100,200).
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        vv_set_idle_mode(&ctx, true);
        vv_set_animation_scale(&ctx, 0.0f); // snap springs so it settles at once

        CHECK(frame(&ctx, vv_v2(20, 50), false) != NULL);       // first build
        CHECK(ctx.last_tier == VV_TIER_BUILD);

        // Hit testing lags geometry by a frame, so the first build resolves hover
        // against an empty tree; the next frame discovers hover=a and rebuilds.
        frame(&ctx, vv_v2(20, 50), false);

        CHECK(frame(&ctx, vv_v2(20, 50), false) == NULL);       // settled -> idle
        CHECK(ctx.last_tier == VV_TIER_IDLE);

        // Move within box "a": hovered node unchanged -> still idle (the point).
        CHECK(frame(&ctx, vv_v2(60, 50), false) == NULL);
        CHECK(ctx.last_tier == VV_TIER_IDLE);

        // Cross into box "b": hovered node changes -> rebuild.
        CHECK(frame(&ctx, vv_v2(150, 50), false) != NULL);
        CHECK(ctx.last_tier == VV_TIER_BUILD);

        // Move within box "b": unchanged again -> idle.
        CHECK(frame(&ctx, vv_v2(170, 50), false) == NULL);
        CHECK(ctx.last_tier == VV_TIER_IDLE);

        // A press is an edge -> wakes even without a hover change.
        CHECK(frame(&ctx, vv_v2(170, 50), true) != NULL);
        CHECK(ctx.last_tier == VV_TIER_BUILD);
        vv_shutdown(&ctx);
    }

    // Idle mode OFF: always returns a buffer (present every frame), never idles.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        vv_set_animation_scale(&ctx, 0.0f);
        frame(&ctx, vv_v2(20, 50), false);       // build
        frame(&ctx, vv_v2(20, 50), false);       // hover settles
        CHECK(frame(&ctx, vv_v2(20, 50), false) != NULL);
        CHECK(ctx.last_tier == VV_TIER_PRESENT); // static, but presents anyway
        vv_shutdown(&ctx);
    }
}

TEST_MAIN()
