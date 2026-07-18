#include "verve/verve.h"
#include "vv_test.h"

// Smoke test: build each widget across frames and drive a click through the
// button to confirm it emits its message into the queue (§14.1).

enum { MSG_BTN = 1, MSG_TOG, MSG_SLIDE, MSG_CHK, MSG_DRAG };

static void body(vv_Ctx *ctx, bool toggle_v, float slider_v) {
    VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 8, .padding = vv_all(10),
                                  .w = vv_fixed(300) }), (vv_Style){0}) {
        vv_button(ctx, "b", "Click", MSG_BTN, VV_NO_PAYLOAD);
        vv_toggle(ctx, "t", toggle_v, MSG_TOG);
        vv_slider(ctx, "s", slider_v, 0, 1, MSG_SLIDE);
        vv_checkbox(ctx, "c", "Check", false, MSG_CHK);
        vv_drag_number(ctx, "d", 1.0f, 0.1f, 0, 10, MSG_DRAG);
    }
}

// True iff `msg` was emitted this frame.
static bool drained(vv_Ctx *ctx, vv_Msg msg) {
    bool got = false;
    vv_Event ev;
    while (vv_poll_event(ctx, &ev)) if (ev.msg == msg) got = true;
    return got;
}

static void run_tests(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 400, 1.0f);

    // Establish geometry.
    for (int f = 0; f < 2; f++) {
        vv_Input in = {0};
        vv_begin_frame(&ctx, 0.016f, &in);
        body(&ctx, false, 0.5f);
        vv_end_frame(&ctx);
        while (vv_poll_event(&ctx, &(vv_Event){0})) {} // clear
    }

    // Button lives near the top-left. Press (no message yet — click needs release).
    {
        vv_Input in = { .mouse = vv_v2(40, 30), .mouse_down = true };
        vv_begin_frame(&ctx, 0.016f, &in);
        body(&ctx, false, 0.5f);
        vv_end_frame(&ctx);
        CHECK(!drained(&ctx, MSG_BTN)); // press alone doesn't click
    }
    // Release inside -> the button emits MSG_BTN.
    {
        vv_Input in = { .mouse = vv_v2(40, 30), .mouse_down = false };
        vv_begin_frame(&ctx, 0.016f, &in);
        body(&ctx, false, 0.5f);
        vv_end_frame(&ctx);
        CHECK(drained(&ctx, MSG_BTN));
    }

    vv_shutdown(&ctx);
}

TEST_MAIN()
