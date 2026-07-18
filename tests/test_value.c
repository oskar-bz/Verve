#include "verve/verve.h"
#include "vv_test.h"

// Value bindings (§12) reconciled with the message model: a bound slider emits
// VV_MSG_BIND, which vv_run_frame applies through the target pointer with no
// update() case. Also checks the perceptual curve, READONLY, and one edit
// commit per drag session (§12.1).

static float g_cutoff = 0.5f;
static vv_ValueMeta g_meta = {.min = 0, .max = 10, .curve = 1.0f};

static void update_fn(void *state, vv_Event ev) { (void)state; (void)ev; }

static void view_fn(vv_Ctx *ctx, void *state) {
    (void)state;
    VV_BOX(ctx, ((vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1), .padding = vv_all(10) }),
           (vv_Style){0}) {
        vv_slider_bound(ctx, "cut", vv_f32(&g_cutoff, &g_meta));
    }
}

static void frame(vv_Ctx *ctx, vv_Vec2 m, bool down) {
    vv_run_frame(ctx, 0.016f, &(vv_Input){ .mouse = m, .mouse_down = down },
                 update_fn, view_fn, NULL);
}

static void run_tests(void) {
    // 1) Apply mechanism writes through the pointer, driven purely by messages.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 100, 1.0f);
        g_cutoff = 5.0f;
        frame(&ctx, vv_v2(-1, -1), false); // settle geometry

        // Track spans x≈10..390 (width 380, pad 10). Press ~3/4 across.
        vv_Vec2 at = vv_v2(10 + 380 * 0.75f, 24);
        frame(&ctx, at, true);   // press+drag -> emits VV_MSG_BIND
        frame(&ctx, at, true);   // driver applies it before this build
        // 0..10 linear, ~0.75 of the way, minus handle inset -> well above 5.
        CHECK(g_cutoff > 6.0f);
        CHECK(g_cutoff <= 10.0f);
        vv_shutdown(&ctx);
    }

    // 2) Curve mapping: curve > 1 gives finer control near min, so the same
    //    normalized position maps to a smaller value than linear would.
    {
        float lo = 0, hi = 100;
        vv_ValueMeta lin = {.min = lo, .max = hi, .curve = 1.0f};
        vv_ValueMeta log = {.min = lo, .max = hi, .curve = 3.0f};
        float mid = 0.5f;
        float v_lin = vv_value_denorm(&lin, lo, hi, mid);
        float v_log = vv_value_denorm(&log, lo, hi, mid);
        CHECK(v_lin > 49.0f && v_lin < 51.0f);   // linear midpoint
        CHECK(v_log < v_lin);                     // curved skews toward min
        // Round-trips: norm(denorm(t)) == t.
        float t = vv_value_norm(&log, lo, hi, v_log);
        CHECK(t > 0.49f && t < 0.51f);
    }

    // 3) READONLY bindings never emit, so nothing is ever applied.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 100, 1.0f);
        float ro = 3.0f;
        vv_ValueMeta rom = {.min = 0, .max = 10, .curve = 1.0f,
                            .flags = VV_VAL_READONLY};
        for (int i = 0; i < 3; i++) {
            vv_begin_frame(&ctx, 0.016f,
                           &(vv_Input){ .mouse = vv_v2(300, 24), .mouse_down = true });
            VV_BOX(&ctx, ((vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1) }), (vv_Style){0})
                vv_slider_bound(&ctx, "ro", vv_f32(&ro, &rom));
            vv_end_frame(&ctx);
            CHECK(!vv_poll_event(&ctx, &(vv_Event){0})); // no bind emitted
        }
        CHECK(ro == 3.0f); // unchanged
        vv_shutdown(&ctx);
    }

    // 4) One edit commit per drag session, not per frame (§12.1).
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 100, 1.0f);
        g_cutoff = 5.0f;
        frame(&ctx, vv_v2(-1, -1), false); // settle
        uint32_t g0 = ctx.edit_generation;
        vv_Vec2 a = vv_v2(120, 24), b = vv_v2(200, 24), c = vv_v2(280, 24);
        frame(&ctx, a, true);  // press -> begin_edit
        frame(&ctx, b, true);  // drag
        frame(&ctx, c, true);  // drag
        CHECK(ctx.edit_generation == g0);     // no commit mid-drag
        frame(&ctx, c, false); // release -> one commit
        CHECK(ctx.edit_generation == g0 + 1);
        vv_shutdown(&ctx);
    }
}

TEST_MAIN()
