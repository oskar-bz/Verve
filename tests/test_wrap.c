#include "verve/verve.h"
#include "vv_test.h"

// Flex-wrap (vv_LayoutDecl.wrap): children flow onto multiple lines when they
// overflow the row width, and the row's height grows to hold every line.

static uint32_t g_row, g_box[5];

static void view(vv_Ctx *c, void *st) {
    (void)st;
    // 200px-wide column; a wrapping row of five 60px boxes, gap 10.
    // Per line: 60, +10+60=130, +10+60=200 (fits), next 270>200 -> wrap.
    // So 3 per line -> two lines (3 + 2).
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .w = vv_fixed(200), .h = vv_fit(),
                               .padding = vv_all(0)}), ((vv_Style){0})) {
        g_row = vv_box_keyed(c, "row", 3,
            (vv_LayoutDecl){.dir = VV_ROW, .w = vv_grow(1), .h = vv_fit(),
                            .wrap = true, .gap = 10},
            (vv_Style){0});
        for (int i = 0; i < 5; i++) {
            g_box[i] = vv_box_keyed(c, vv_fmt(c, "b%d", i), 0,
                (vv_LayoutDecl){.w = vv_fixed(60), .h = vv_fixed(20)}, (vv_Style){0});
            vv_end_box(c);
        }
        vv_end_box(c); // row
    }
}

int main(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 400, 400, 1.0f);
    vv_set_animation_scale(&ctx, 0.0f);
    vv_Input in = {0};
    vv_begin_frame(&ctx, 0.016f, &in); view(&ctx, NULL); vv_end_frame(&ctx);
    vv_begin_frame(&ctx, 0.016f, &in); view(&ctx, NULL); vv_end_frame(&ctx);

    // Row height should be two lines of 20 + one 10 gap = 50.
    vv_Rect row = vv_pool_get(&ctx.pool, g_row)->actual_rect;
    CHECK_NEAR(row.h, 50.0f, 0.5f);

    // First three boxes on line 0 (y equal); last two wrap to line 1.
    float y0 = vv_pool_get(&ctx.pool, g_box[0])->actual_rect.y;
    float y3 = vv_pool_get(&ctx.pool, g_box[3])->actual_rect.y;
    CHECK_NEAR(vv_pool_get(&ctx.pool, g_box[1])->actual_rect.y, y0, 0.5f);
    CHECK_NEAR(vv_pool_get(&ctx.pool, g_box[2])->actual_rect.y, y0, 0.5f);
    CHECK_NEAR(y3 - y0, 30.0f, 0.5f);   // line 1 is 20 (height) + 10 (gap) below
    CHECK_NEAR(vv_pool_get(&ctx.pool, g_box[4])->actual_rect.y, y3, 0.5f);
    // Box 3 wraps back to the left edge.
    CHECK_NEAR(vv_pool_get(&ctx.pool, g_box[3])->actual_rect.x,
               vv_pool_get(&ctx.pool, g_box[0])->actual_rect.x, 0.5f);

    vv_shutdown(&ctx);
    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n");
    return 0;
}
