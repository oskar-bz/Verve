#include "verve/verve.h"
#include "vv_test.h"
#include <stdio.h>

// Virtualization (§5.5): a 1000-row list must build only the visible slice and
// never accumulate nodes as it scrolls.

static uint32_t g_scroll;

static void rowfn(vv_Ctx *c, int i, void *ud) {
    (void)ud;
    char b[24]; snprintf(b, sizeof b, "Row %d", i);
    vv_text(c, b, (vv_Style){ .fg = vv_rgb(1,1,1), .font_size = 14 });
}

static void build(vv_Ctx *c) {
    g_scroll = vv_box_keyed(c, "sc", 2,
        (vv_LayoutDecl){ .dir = VV_COLUMN, .w = vv_fixed(300), .h = vv_fixed(200),
                         .scroll_y = true, .clip = true }, (vv_Style){0});
    vv_rows(c, 1000, 30.0f, rowfn, NULL);
    vv_end_box(c);
}

static void run_tests(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 320, 240, 1.0f);
    vv_Input in = {0};

    for (int f = 0; f < 3; f++) { vv_begin_frame(&ctx, 0.016f, &in); build(&ctx); vv_end_frame(&ctx); }

    // Only a small slice is live (200px / 30px ≈ 7 rows + overscan + spacers +
    // container + text nodes), nowhere near 1000.
    uint32_t live_top = ctx.pool.map_len;
    CHECK(live_top < 80);

    // Scroll to the middle by driving the scroll spring directly.
    vv_Node *sc = vv_node(&ctx, g_scroll);
    sc->scroll_y.x = sc->scroll_y.target = 15000.0f; // row ~500
    sc->scroll_y.settled = true;

    for (int f = 0; f < 3; f++) { vv_begin_frame(&ctx, 0.016f, &in); build(&ctx); vv_end_frame(&ctx); }

    // Still bounded — culled rows were freed immediately, not leaked.
    CHECK(ctx.pool.map_len < 80);

    // Scroll to the very bottom.
    sc = vv_node(&ctx, g_scroll);
    sc->scroll_y.x = sc->scroll_y.target = 29800.0f;
    sc->scroll_y.settled = true;
    for (int f = 0; f < 3; f++) { vv_begin_frame(&ctx, 0.016f, &in); build(&ctx); vv_end_frame(&ctx); }
    CHECK(ctx.pool.map_len < 80);

    vv_shutdown(&ctx);
}

TEST_MAIN()
