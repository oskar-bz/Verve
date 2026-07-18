// headless_demo.c — drive the core with no GPU, print the command buffer.
// Proves the whole Phase 0 pipeline end to end without a windowing backend.
#include "verve/verve.h"
#include <stdio.h>

// A trivial backend that just describes what it's asked to draw.
static void be_begin(void *c, int w, int h, float s) {
    (void)c; printf("BEGIN %dx%d @%.1fx\n", w, h, (double)s);
}
static void be_end(void *c) { (void)c; printf("END\n"); }
static void be_rects(void *c, const vv_CmdRect *r, int n) {
    (void)c;
    for (int i = 0; i < n; i++)
        printf("  RECT   (%.0f,%.0f %.0fx%.0f) bg=(%.2f,%.2f,%.2f,%.2f) r=%.0f\n",
               (double)r[i].rect.x, (double)r[i].rect.y, (double)r[i].rect.w,
               (double)r[i].rect.h, (double)r[i].fill_a.r, (double)r[i].fill_a.g,
               (double)r[i].fill_a.b, (double)r[i].fill_a.a, (double)r[i].radius.tl);
}
static void be_text(void *c, const vv_CmdText *t, int n) {
    (void)c;
    for (int i = 0; i < n; i++)
        printf("  TEXT   \"%.*s\" @(%.0f,%.0f) size=%.0f\n",
               (int)t[i].len, t[i].utf8, (double)t[i].origin.x,
               (double)t[i].origin.y, (double)t[i].size);
}

int main(void) {
    vv_Ctx ctx;
    vv_init(&ctx);
    vv_set_window(&ctx, 640, 480, 1.0f);

    vv_Backend be = {
        .begin = be_begin, .end = be_end,
        .draw_rects = be_rects, .draw_text = be_text,
    };

    vv_Style theme_surface = { .bg = vv_rgb(0.12f, 0.12f, 0.14f), .radius = vv_r(8) };
    vv_Style accent        = { .bg = vv_rgb(0.20f, 0.55f, 0.95f), .radius = vv_r(6) };

    vv_Input in = {0};
    for (int frame = 0; frame < 2; frame++) {
        printf("\n=== frame %d ===\n", frame);
        vv_begin_frame(&ctx, 0.016f, &in);

        VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 8,
                                       .padding = vv_all(12) }), theme_surface) {
            vv_text(&ctx, "TEMPERATURE CONVERTER", (vv_Style){ .fg = vv_rgb(1,1,1) });
            VV_BOX(&ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 8,
                                           .w = vv_fixed(200), .h = vv_fixed(32) }), accent) {
                vv_text(&ctx, "Celsius", (vv_Style){ .fg = vv_rgb(1,1,1) });
            }
            // A conditionally-present, keyed row (would churn without the key).
            if (frame == 1) {
                vv_box_keyed(&ctx, "fahrenheit", 10,
                    (vv_LayoutDecl){ .w = vv_fixed(200), .h = vv_fixed(32) }, accent);
                vv_end_box(&ctx);
            }
        }

        vv_CommandBuffer *cmds = vv_end_frame(&ctx);
        vv_render(&be, cmds, (int)ctx.win_w, (int)ctx.win_h, ctx.dpi_scale);
        printf("(%u commands, %u unsettled springs)\n",
               cmds->count, ctx.unsettled_springs);
    }

    vv_shutdown(&ctx);
    return 0;
}
