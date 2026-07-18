#include "verve/verve.h"
#include "vv_test.h"

// Pointer-move subscription (vv_On.move): only a hovered node that subscribed
// rebuilds on plain motion, and the emitted event carries the cursor position.
// An unsubscribed node stays idle when the pointer moves within it.

enum { MSG_MOVE = 1 };

static int     g_moves;
static vv_Vec2 g_last;
static bool    g_sub;

static void update_fn(void *s, vv_Event e) {
    (void)s;
    if (e.msg == MSG_MOVE) { g_moves++; g_last = vv_as_v2(e.data); }
}

static void view_fn(vv_Ctx *ctx, void *s) {
    (void)s;
    uint32_t id = vv_box_keyed(ctx, "a", 1,
        (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1), .focusable = true },
        (vv_Style){ .bg = vv_rgb(0.2f, 0.2f, 0.2f) });
    if (g_sub) vv_on(ctx, id, (vv_On){ .move = MSG_MOVE });
    vv_end_box(ctx);
}

static vv_FrameTier frame(vv_Ctx *ctx, vv_Vec2 m) {
    vv_run_frame(ctx, 0.016f, &(vv_Input){ .mouse = m }, update_fn, view_fn, NULL);
    return ctx->last_tier;
}

static void run_tests(void) {
    // Subscribed: motion within the node emits the cursor and rebuilds.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        vv_set_idle_mode(&ctx, true); vv_set_animation_scale(&ctx, 0.0f);
        g_sub = true; g_moves = 0; g_last = vv_v2(0, 0);

        frame(&ctx, vv_v2(10, 10));           // build
        frame(&ctx, vv_v2(10, 10));           // hover settles
        CHECK(frame(&ctx, vv_v2(10, 10)) == VV_TIER_IDLE);  // still, subscribed but no motion

        vv_FrameTier t = frame(&ctx, vv_v2(40, 60)); // motion over subscriber
        CHECK(t == VV_TIER_BUILD);
        CHECK(g_moves >= 1);
        CHECK(g_last.x == 40.0f && g_last.y == 60.0f);     // cursor delivered

        CHECK(frame(&ctx, vv_v2(40, 60)) == VV_TIER_IDLE); // no further motion
        vv_shutdown(&ctx);
    }

    // Not subscribed: the same motion changes nothing -> stays idle, no event.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        vv_set_idle_mode(&ctx, true); vv_set_animation_scale(&ctx, 0.0f);
        g_sub = false; g_moves = 0;

        frame(&ctx, vv_v2(10, 10));
        frame(&ctx, vv_v2(10, 10));
        CHECK(frame(&ctx, vv_v2(10, 10)) == VV_TIER_IDLE);
        CHECK(frame(&ctx, vv_v2(40, 60)) == VV_TIER_IDLE); // motion within same node
        CHECK(g_moves == 0);
        vv_shutdown(&ctx);
    }
}

TEST_MAIN()
