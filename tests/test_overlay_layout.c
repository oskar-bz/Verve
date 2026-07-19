#include "verve/verve.h"
#include "vv_test.h"

// Regressions for two positioning/lifecycle bugs:
//  1) A z-lifted absolute overlay is positioned in WINDOW space, not offset by
//     its (possibly deeply nested) build parent's origin.
//  2) Virtualized rows (and their children, and the spacers) leave no
//     exit-animating corpses when scrolled — they free immediately.

// ---- 1: overlay absolute is screen-space -----------------------------------
static uint32_t g_ov;
static void ov_view(vv_Ctx *c, void *st) {
    (void)st;
    // Nest the overlay deep so a parent-relative bug would be obvious.
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                               .padding = vv_all(50)}), ((vv_Style){0})) {
        VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .padding = vv_all(40)}), ((vv_Style){0})) {
            g_ov = vv_box_keyed(c, "ov", 2,
                (vv_LayoutDecl){.has_absolute = true, .z = 1000,
                                .absolute = vv_rect(12, 34, 100, 80)},
                (vv_Style){.bg = vv_rgb(1, 0, 0)});
            vv_end_box(c);
        }
    }
}

// ---- 2: virtualized scroll leaves no corpses -------------------------------
static void row(vv_Ctx *c, int i, void *ud) {
    (void)ud;
    vv_text(c, vv_fmt(c, "Row %d", i), (vv_Style){.fg = vv_rgb(1, 1, 1), .font_size = 14});
}
static void list_view(vv_Ctx *c, void *st) {
    (void)st;
    VV_BOX(c, ((vv_LayoutDecl){.w = vv_grow(1), .h = vv_grow(1), .scroll_y = true, .clip = true}),
           ((vv_Style){0})) {
        vv_rows(c, 1000, 30.0f, row, NULL);
    }
}
static int count_exiting(vv_Ctx *ctx) {
    int n = 0;
    for (uint32_t i = 0; i < ctx->pool.count; i++) {
        vv_Node *nd = &ctx->pool.nodes[i];
        if ((nd->flags & VV_FLAG_ALIVE) && (nd->flags & VV_FLAG_EXITING)) n++;
    }
    return n;
}

int main(void) {
    // 1) overlay position: window is 400x400; overlay absolute is (12,34), and
    // since it's z-lifted it should land exactly there regardless of the 90px of
    // nested padding above it.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 400, 1.0f);
        vv_Input in = {0};
        vv_begin_frame(&ctx, 0.016f, &in); ov_view(&ctx, NULL); vv_end_frame(&ctx);
        vv_begin_frame(&ctx, 0.016f, &in); ov_view(&ctx, NULL); vv_end_frame(&ctx);
        vv_Rect r = vv_pool_get(&ctx.pool, g_ov)->actual_rect;
        CHECK_NEAR(r.x, 12.0f, 0.5f);   // NOT 12 + 90 of ancestor padding
        CHECK_NEAR(r.y, 34.0f, 0.5f);
        vv_shutdown(&ctx);
    }

    // 2) virtualized scroll churns rows; nothing should be left exit-animating.
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 300, 300, 1.0f);
        vv_Input in = {.mouse = vv_v2(150, 150)};
        for (int i = 0; i < 5; i++) { vv_begin_frame(&ctx, 0.016f, &in); list_view(&ctx, NULL); vv_end_frame(&ctx); }
        int maxexit = 0;
        for (int s = 0; s < 40; s++) {
            vv_Input w = {.mouse = vv_v2(150, 150), .wheel = -3};
            vv_begin_frame(&ctx, 0.016f, &w); list_view(&ctx, NULL); vv_end_frame(&ctx);
            int e = count_exiting(&ctx); if (e > maxexit) maxexit = e;
        }
        CHECK(maxexit == 0);
        vv_shutdown(&ctx);
    }

    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n");
    return 0;
}
