#include "verve/verve.h"
#include "vv_test.h"

// Overlay layer (§ overlays): a node with decl.z > 0 paints above the normal
// tree and — crucially — must also win hit testing, even when a later-built
// sibling covers the same point. Also covers vv_ui_state persistence.

static uint32_t g_overlay, g_cover;

static void build(vv_Ctx *ctx) {
    vv_Style s = {.bg = vv_rgb(0.3f, 0.3f, 0.3f)};
    VV_BOX(ctx, ((vv_LayoutDecl){.w = vv_fixed(200), .h = vv_fixed(200)}), s) {
        // Overlay declared FIRST, lifted with z. Covers (0,0)-(100,100).
        g_overlay = vv_box_keyed(ctx, "ov", 2,
            (vv_LayoutDecl){.has_absolute = true, .z = 1000, .focusable = true,
                            .absolute = vv_rect(0, 0, 100, 100)}, s);
        vv_end_box(ctx);
        // A later sibling covering the same point, no z. In plain tree order it
        // would win the hit; the overlay must beat it.
        g_cover = vv_box_keyed(ctx, "cover", 5,
            (vv_LayoutDecl){.has_absolute = true, .focusable = true,
                            .absolute = vv_rect(0, 0, 100, 100)}, s);
        vv_end_box(ctx);
    }
}

static void frame(vv_Ctx *ctx, float mx, float my) {
    vv_Input in = {.mouse = vv_v2(mx, my)};
    vv_begin_frame(ctx, 0.016f, &in);
    build(ctx);
    vv_end_frame(ctx);
}

int main(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 200, 1.0f);

    frame(&ctx, 0, 0);       // establish geometry
    frame(&ctx, 50, 50);     // hover the overlapped region
    CHECK(vv_hovered(&ctx, g_overlay));   // z lifts it above the later sibling
    CHECK(!vv_hovered(&ctx, g_cover));

    // Point inside the covering node but the overlay covers it too -> overlay.
    frame(&ctx, 90, 90);
    CHECK(vv_hovered(&ctx, g_overlay));

    // vv_ui_state: string-keyed, zero-initialized, persists across frames.
    int *n = vv_ui_state(&ctx, "counter", int);
    CHECK(*n == 0);
    *n = 42;
    int *again = vv_ui_state(&ctx, "counter", int);
    CHECK(again == n);        // same block
    CHECK(*again == 42);      // value persisted
    int *other = vv_ui_state(&ctx, "other", int);
    CHECK(other != n);        // distinct key -> distinct block
    CHECK(*other == 0);

    vv_shutdown(&ctx);
    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n");
    return 0;
}
