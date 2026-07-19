#include "verve/verve.h"
#include "vv_test.h"

// Right-click, cursor-shape resolution, and combobox open/pick.

static uint32_t g_box, g_combo;
static int g_combo_cur = 0;

static void view_box(vv_Ctx *c, void *st) {
    (void)st;
    g_box = vv_box_keyed(c, "b", 1,
        (vv_LayoutDecl){.w = vv_fixed(100), .h = vv_fixed(60), .focusable = true,
                        .cursor = VV_CURSOR_POINTER}, (vv_Style){.bg = vv_rgb(.3f,.3f,.3f)});
    vv_end_box(c);
}

static void view_combo(vv_Ctx *c, void *st) {
    (void)st;
    static const char *const opts[] = {"Alpha", "Beta", "Gamma"};
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .w = vv_grow(1), .padding = vv_all(10)}), ((vv_Style){0})) {
        g_combo = vv_combobox(c, "cb", opts, 3, g_combo_cur, 9 /*MSG*/);
    }
}

static void frame(vv_Ctx *c, vv_ViewFn v, vv_Vec2 m, bool ldown, bool rdown) {
    vv_Input in = {.mouse = m, .mouse_down = ldown, .right_down = rdown};
    vv_begin_frame(c, 0.016f, &in); v(c, NULL); vv_end_frame(c);
}

int main(void) {
    // --- right-click + cursor ---
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 200, 1.0f);
        frame(&ctx, view_box, vv_v2(0, 0), false, false);   // geometry
        frame(&ctx, view_box, vv_v2(50, 30), false, false); // hover
        CHECK(vv_cursor(&ctx) == VV_CURSOR_POINTER);         // hovered node's cursor
        frame(&ctx, view_box, vv_v2(50, 30), false, true);   // right press
        frame(&ctx, view_box, vv_v2(50, 30), false, false);  // right release inside
        CHECK(vv_right_clicked(&ctx, g_box));
        frame(&ctx, view_box, vv_v2(50, 30), false, false);  // only fires once
        CHECK(!vv_right_clicked(&ctx, g_box));
        // cursor clears when not hovering the node
        frame(&ctx, view_box, vv_v2(180, 180), false, false);
        CHECK(vv_cursor(&ctx) == VV_CURSOR_DEFAULT);
        vv_shutdown(&ctx);
    }

    // --- combobox open + pick ---
    {
        vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 240, 300, 1.0f);
        frame(&ctx, view_combo, vv_v2(0, 0), false, false);
        vv_Rect cr = vv_pool_get(&ctx.pool, g_combo)->actual_rect;
        float cx = cr.x + cr.w / 2, cy = cr.y + cr.h / 2;
        // click to open
        frame(&ctx, view_combo, vv_v2(cx, cy), true, false);
        frame(&ctx, view_combo, vv_v2(cx, cy), false, false);
        frame(&ctx, view_combo, vv_v2(cx, cy), false, false); // settle list geometry
        // find an option row (focusable, below the field, ~28 tall)
        uint32_t opt = VV_NIL;
        for (uint32_t i = 0; i < ctx.pool.count; i++) {
            vv_Node *n = &ctx.pool.nodes[i];
            if (!(n->flags & VV_FLAG_ALIVE) || (n->flags & VV_FLAG_EXITING) || !n->decl.focusable) continue;
            vv_Rect r = n->actual_rect;
            if (r.h > 26 && r.h < 30 && r.y > cr.y + cr.h) { opt = i; break; }
        }
        CHECK(opt != VV_NIL);
        vv_Rect orr = vv_pool_get(&ctx.pool, opt)->actual_rect;
        frame(&ctx, view_combo, vv_v2(orr.x + orr.w / 2, orr.y + orr.h / 2), true, false);
        frame(&ctx, view_combo, vv_v2(orr.x + orr.w / 2, orr.y + orr.h / 2), false, false);
        vv_Event ev; int picks = 0;
        while (vv_poll_event(&ctx, &ev)) if (ev.msg == 9) picks++;
        CHECK(picks == 1); // exactly one selection message
        vv_shutdown(&ctx);
    }

    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n");
    return 0;
}
