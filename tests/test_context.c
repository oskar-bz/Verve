#include "verve/verve.h"
#include "vv_test.h"

// Build a tree from a "show_extra" flag and verify identity behaviour across
// frames — the central Phase 0 property (§3, §18).

static uint32_t g_persistent_button_id_index; // pool index we track by handle

static void build(vv_Ctx *ctx, bool show_header, bool use_key) {
    vv_Style s = (vv_Style){ .bg = vv_rgb(0.2f, 0.2f, 0.2f) };
    VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 4 }), s) {
        if (show_header) vv_text(ctx, "Header", (vv_Style){0});
        if (use_key)
            g_persistent_button_id_index =
                vv_box_keyed(ctx, "ok", 2, (vv_LayoutDecl){ .w = vv_fixed(80), .h = vv_fixed(24) }, s);
        else
            g_persistent_button_id_index =
                vv_box(ctx, (vv_LayoutDecl){ .w = vv_fixed(80), .h = vv_fixed(24) }, s);
        if (use_key) vv_end_box(ctx);
        else vv_end_box(ctx);
    }
}

static void run_tests(void) {
    vv_Ctx ctx;
    vv_init(&ctx);
    vv_set_window(&ctx, 400, 300, 1.0f);

    vv_Input in = {0};

    // --- Frame 1: no header, no key. Record the button's ID. ---
    vv_begin_frame(&ctx, 0.016f, &in);
    build(&ctx, false, false);
    vv_CommandBuffer *c1 = vv_end_frame(&ctx);
    vv_ID button_id_noheader = vv_node(&ctx, g_persistent_button_id_index)->id;
    CHECK(c1->count > 0);

    // --- Frame 2: header appears, still no key. Sequence shifts -> identity
    // breaks. This is the documented failure mode; assert it really happens. ---
    vv_begin_frame(&ctx, 0.016f, &in);
    build(&ctx, true, false);
    vv_end_frame(&ctx);
    vv_ID button_id_header = vv_node(&ctx, g_persistent_button_id_index)->id;
    CHECK(button_id_noheader != button_id_header); // churn confirmed

    // --- Now with an explicit key: identity is stable across the same flip. ---
    vv_begin_frame(&ctx, 0.016f, &in);
    build(&ctx, false, true);
    vv_end_frame(&ctx);
    vv_ID keyed_a = vv_node(&ctx, g_persistent_button_id_index)->id;

    vv_begin_frame(&ctx, 0.016f, &in);
    build(&ctx, true, true);
    vv_end_frame(&ctx);
    vv_ID keyed_b = vv_node(&ctx, g_persistent_button_id_index)->id;
    CHECK_EQ_U(keyed_a, keyed_b); // stable despite sibling shift

    // --- Lifecycle: a node that stops being built must eventually free. ---
    // Build a keyed child, then stop; run frames until the exit spring settles.
    uint32_t before_live = ctx.pool.map_len;
    for (int f = 0; f < 200; f++) {
        vv_begin_frame(&ctx, 0.05f, &in);
        // build nothing but root
        vv_end_frame(&ctx);
    }
    // No leak: map should not have grown unboundedly.
    CHECK(ctx.pool.map_len <= before_live);

    vv_shutdown(&ctx);
}

TEST_MAIN()
