#include "verve/verve.h"
#include "vv_test.h"

// Drives vv_date_field headlessly: open the calendar, click a day, and assert
// that exactly one correctly-packed message is emitted — i.e. all the widget's
// internal churn (open, month grid) stays internal.

static uint32_t g_field;
static const int32_t START = 20260719; // 2026-07-19

static void build(vv_Ctx *ctx) {
    VV_BOX(ctx, ((vv_LayoutDecl){.dir = VV_COLUMN, .w = vv_grow(1), .padding = vv_all(10)}),
           ((vv_Style){0})) {
        g_field = vv_date_field(ctx, "d", START, 7 /*MSG*/);
    }
}

static void frame(vv_Ctx *ctx, float mx, float my, bool down) {
    vv_Input in = {.mouse = vv_v2(mx, my), .mouse_down = down};
    vv_begin_frame(ctx, 0.016f, &in);
    build(ctx);
    vv_end_frame(ctx);
}

// A day cell is a focusable ~32x28 box; pad cells aren't focusable, nav buttons
// are 26x24, the field is full-width — so this uniquely finds day cells.
static uint32_t find_day_cell(vv_Ctx *ctx) {
    for (uint32_t i = 0; i < ctx->pool.count; i++) {
        vv_Node *n = &ctx->pool.nodes[i];
        if (!(n->flags & VV_FLAG_ALIVE) || (n->flags & VV_FLAG_EXITING)) continue;
        if (!n->decl.focusable) continue;
        vv_Rect r = n->actual_rect;
        if (r.w > 30 && r.w < 34 && r.h > 26 && r.h < 30) return i;
    }
    return VV_NIL;
}

int main(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 320, 420, 1.0f);

    frame(&ctx, 0, 0, false);                 // establish geometry
    vv_Rect fr = vv_pool_get(&ctx.pool, g_field)->actual_rect;
    float fx = fr.x + fr.w / 2, fy = fr.y + fr.h / 2;

    // Calendar starts closed: no day cells in the tree.
    CHECK(find_day_cell(&ctx) == VV_NIL);

    // Click the field (press then release) to open it.
    frame(&ctx, fx, fy, true);
    frame(&ctx, fx, fy, false);
    frame(&ctx, fx, fy, false);               // let the calendar's geometry settle

    uint32_t cell = find_day_cell(&ctx);
    CHECK(cell != VV_NIL);                     // opening built the day grid

    vv_Rect cr = vv_pool_get(&ctx.pool, cell)->actual_rect;
    float cx = cr.x + cr.w / 2, cy = cr.y + cr.h / 2;

    // Click a day. The click emits during that build; drain and inspect.
    frame(&ctx, cx, cy, true);
    frame(&ctx, cx, cy, false);

    vv_Event ev; int got = 0; int32_t packed = 0;
    while (vv_poll_event(&ctx, &ev)) { if (ev.msg == 7) { got++; packed = (int32_t)ev.data.as_int; } }
    CHECK(got == 1);                           // exactly one outward message

    int y, m, d;
    vv_date_unpack(packed, &y, &m, &d);
    CHECK(y == 2026);                          // same month we opened on
    CHECK(m == 7);
    CHECK(d >= 1 && d <= 31);

    // Picking a day closes the calendar again.
    frame(&ctx, 0, 0, false);
    CHECK(find_day_cell(&ctx) == VV_NIL);

    vv_shutdown(&ctx);
    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n");
    return 0;
}
