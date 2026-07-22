// view.c — the hot-reloaded UI. This is the file you edit while the app runs.
//
//   Terminal 1:  make template-hot   ->   ./build/tpl_hotdemo
//   Terminal 2:  edit this file, then `make template-hot-view`
//                -> the running window updates, keeping App state (the count).
//
// Only the public API is used here — the same update/view signatures the host's
// vv_hot_run expects. Try changing the label, colours, or layout and rebuilding.
#include "app.h"

void view_update(void *state, vv_Event ev) {
  App *s = state;
  switch (ev.msg) {
  case MSG_INC:   s->count += (int)ev.data.as_int; break;
  case MSG_RESET: s->count = 0;                    break;
  default: break;
  }
}

void view_build(vv_Ctx *c, void *state) {
  App *s = state;
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 14),
         VV_STYLE(.bg = t->surface_app)) {
    vv_text(c, "Edit me and rebuild!", VV_STYLE(.fg = t->text_primary, .font_size = 26));
    vv_text(c, vv_fmt(c, "%d", s->count),
            VV_STYLE(.fg = t->brand_primary, .font_size = 44));
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
      vv_button(c, "inc",   "+1",    MSG_INC, vv_pi(1));
      vv_button(c, "inc10", "+10",   MSG_INC, vv_pi(10));
      vv_button(c, "reset", "Reset", MSG_RESET, VV_NO_PAYLOAD);
    }
  }
}
