// devtools.c — a Verve app with the built-in devtools attached. Press F12 for
// the node inspector and F11 for the performance HUD; each opens in its own
// native window. Build with `make VV_PERF=1 ...` (or `VV_PERF=1 make run ...`)
// for live HUD numbers; without it the HUD shows how to enable them.
//
//   Build:  make run APP=<name>        (after: make new NAME=<name> KIND=devtools)
#include "vv_sdl_gl.h"

enum { MSG_STEP = 1 };

typedef struct { int count; } App;

static void update(void *state, vv_Event ev) {
  App *a = state;
  if (ev.msg == MSG_STEP) a->count += (int)ev.data.as_int;
}

static void view(vv_Ctx *c, void *state) {
  App *a = state;
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(24), .gap = 16),
         VV_STYLE(.bg = t->surface_app)) {
    vv_text(c, "Devtools demo", VV_STYLE(.fg = t->text_primary, .font_size = 24));
    vv_text(c, "F12 = inspector   \xc2\xb7   F11 = performance HUD",
            VV_STYLE(.fg = t->text_muted, .font_size = 13));
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER),
           VV_STYLE(.bg = {0})) {
      vv_button(c, "dec", "-1", MSG_STEP, vv_pi(-1));
      vv_text(c, vv_fmt(c, "%d", a->count),
              VV_STYLE(.fg = t->brand_primary, .font_size = 28));
      vv_button(c, "inc", "+1", MSG_STEP, vv_pi(1));
    }
  }
}

int main(void) {
  App state = {0};
  return vv_app_run(&(vv_AppDesc){
      .title    = "Verve \xc2\xb7 Devtools",
      .width    = 720, .height = 480,
      .update   = update, .view = view, .state = &state,
      .devtools = true,   // F12 inspector window, F11 perf HUD window
  });
}
