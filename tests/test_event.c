#include "verve/verve.h"
#include "vv_test.h"

// The message/update/view machinery: queue FIFO, the vv_run_frame pipeline,
// double-click, and that idle frames present without reaping the tree.

enum { MSG_INC = 1, MSG_DBL = 2 };

// ---- a tiny update/view app ----------------------------------------------
static void update_fn(void *state, vv_Event ev) {
    int *count = state;
    if (ev.msg == MSG_INC) *count += (int)ev.data.as_int;
}

static void view_fn(vv_Ctx *ctx, void *state) {
    (void)state;
    VV_BOX(ctx, ((vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1) }), (vv_Style){0}) {
        vv_button(ctx, "b", "Go", MSG_INC, vv_pi(1));
    }
}

static int count_flag(vv_Ctx *ctx, uint32_t flag) {
    int n = 0;
    for (uint32_t i = 0; i < ctx->pool.count; i++)
        if (ctx->pool.nodes[i].flags & flag) n++;
    return n;
}

static void run_tests(void) {
    // 1) Raw queue is FIFO and preserves payloads.
    {
        vv_Ctx ctx; vv_init(&ctx);
        vv_emit(&ctx, 10, vv_pi(1));
        vv_emit(&ctx, 20, vv_pf(2.5));
        vv_emit(&ctx, VV_MSG_NONE, vv_pi(9)); // ignored
        vv_Event ev;
        CHECK(vv_poll_event(&ctx, &ev) && ev.msg == 10 && ev.data.as_int == 1);
        CHECK(vv_poll_event(&ctx, &ev) && ev.msg == 20 && ev.data.as_float == 2.5);
        CHECK(!vv_poll_event(&ctx, &ev)); // VV_MSG_NONE was never enqueued
        vv_shutdown(&ctx);
    }

    // 2) vv_run_frame pipeline: a click emits, the message reaches update() next
    //    frame (one-frame pipeline), and state advances exactly once.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        int count = 0;
        vv_Vec2 at = vv_v2(20, 20);
        // settle geometry
        vv_run_frame(&ctx, 0.016f, &(vv_Input){0}, update_fn, view_fn, &count);
        // press then release on the button
        vv_run_frame(&ctx, 0.016f, &(vv_Input){ .mouse = at, .mouse_down = true },
                     update_fn, view_fn, &count);
        vv_run_frame(&ctx, 0.016f, &(vv_Input){ .mouse = at, .mouse_down = false },
                     update_fn, view_fn, &count); // click emitted here
        CHECK(count == 0);                          // not yet processed
        vv_run_frame(&ctx, 0.016f, &(vv_Input){ .mouse = at }, update_fn, view_fn, &count);
        CHECK(count == 1);                          // drained + applied once
        vv_shutdown(&ctx);
    }

    // 3) Idle frame presents without rebuilding or reaping the tree.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        int count = 0;
        vv_run_frame(&ctx, 0.016f, &(vv_Input){0}, update_fn, view_fn, &count); // build
        int alive = count_flag(&ctx, VV_FLAG_ALIVE);
        CHECK(alive > 0);
        // No input, no events: should present-only.
        vv_run_frame(&ctx, 0.016f, &(vv_Input){0}, update_fn, view_fn, &count);
        CHECK(ctx.last_tier == VV_TIER_PRESENT);
        CHECK(count_flag(&ctx, VV_FLAG_ALIVE) == alive); // tree intact
        CHECK(count_flag(&ctx, VV_FLAG_EXITING) == 0);   // nothing reaped
        vv_shutdown(&ctx);
    }

    // 4) Double-click: a second click on the same node within the window is
    //    reported by vv_double_clicked.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 100, 1.0f);
        vv_Vec2 at = vv_v2(20, 20);
        bool saw_double = false;
        for (int step = 0; step < 5; step++) {
            // steps: 0 settle, 1 press, 2 release, 3 press, 4 release(double)
            bool down = (step == 1 || step == 3);
            vv_Input in = { .mouse = at, .mouse_down = down };
            vv_begin_frame(&ctx, 0.016f, &in);
            uint32_t id = vv_box_keyed(&ctx, "hit", 3,
                (vv_LayoutDecl){ .w = vv_fixed(100), .h = vv_fixed(40), .focusable = true },
                (vv_Style){0});
            vv_end_box(&ctx);
            if (vv_double_clicked(&ctx, id)) saw_double = true;
            vv_end_frame(&ctx);
        }
        CHECK(saw_double);
        vv_shutdown(&ctx);
    }
}

TEST_MAIN()
