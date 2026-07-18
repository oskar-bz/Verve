#include "verve/verve.h"
#include "vv_test.h"
#include <string.h>

// Drive the text field editor headlessly: focus, type, navigate, delete.

static char buf[64];

static void frame(vv_Ctx *ctx, vv_Input in) {
    vv_begin_frame(ctx, 0.016f, &in);
    VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .padding = vv_all(10),
                                  .w = vv_fixed(300) }), (vv_Style){0}) {
        vv_text_field(ctx, "f", buf, sizeof buf, "type...");
    }
    vv_end_frame(ctx);
}

static vv_Input type_text(const char *s) {
    vv_Input in = { .mouse = vv_v2(50, 20) };
    in.text_len = (uint8_t)strlen(s);
    memcpy(in.text, s, in.text_len);
    return in;
}
static vv_Input press_key(vv_Key k, bool shift, bool ctrl) {
    vv_Input in = { .mouse = vv_v2(50, 20) };
    in.key_count = 1;
    in.keys[0] = (vv_KeyEvent){ (uint16_t)k, shift, ctrl };
    return in;
}

static void run_tests(void) {
    vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 320, 120, 1.0f);
    buf[0] = 0;

    // Establish geometry.
    frame(&ctx, (vv_Input){ .mouse = vv_v2(50, 20) });
    // Press + release to focus the field.
    frame(&ctx, (vv_Input){ .mouse = vv_v2(50, 20), .mouse_down = true });
    frame(&ctx, (vv_Input){ .mouse = vv_v2(50, 20), .mouse_down = false });

    // Type "Hello".
    frame(&ctx, type_text("Hello"));
    CHECK(strcmp(buf, "Hello") == 0);

    // Backspace removes last char.
    frame(&ctx, press_key(VV_KEY_BACKSPACE, false, false));
    CHECK(strcmp(buf, "Hell") == 0);

    // Home, then type at start.
    frame(&ctx, press_key(VV_KEY_HOME, false, false));
    frame(&ctx, type_text("X"));
    CHECK(strcmp(buf, "XHell") == 0);

    // Cursor sits after 'X' (index 1); Right -> index 2; Delete removes 'e'.
    frame(&ctx, press_key(VV_KEY_RIGHT, false, false));
    frame(&ctx, press_key(VV_KEY_DELETE, false, false));
    CHECK(strcmp(buf, "XHll") == 0);

    // Ctrl+A select all, then type replaces selection.
    frame(&ctx, press_key(VV_KEY_A, false, true));
    frame(&ctx, type_text("Y"));
    CHECK(strcmp(buf, "Y") == 0);

    vv_shutdown(&ctx);
}

TEST_MAIN()
