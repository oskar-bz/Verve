// view.c — the hot-reloaded UI. Edit this, `make hot` (or your watcher), and the
// running window updates without losing App state. No verve internals needed;
// it only uses the public API, same signatures vv_run_frame expects.
#include "app.h"


void view_update(void *state, vv_Event ev) {
  App *s = state;
  switch (ev.msg) {
  case MSG_STEP:   s->count += ev.data.as_int;   break;
  case MSG_TOGGLE: s->subtract = ev.data.as_int; break;
  case MSG_RESET:  s->count = 0;                 break;
  }
}

void view_build(vv_Ctx *c, void *state) {
  App *s = state;
  const vv_Theme *t = vv_theme();
  int64_t step = s->subtract ? -1 : 1;
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 12),
         VV_STYLE(.bg = vv_rgb(0.11f, 0.12f, 0.14f))) {
    // Try editing this label or the colors above and rebuilding the .so.
    vv_text(c, "Very Hot reload!", VV_STYLE(.fg = t->text, .font_size = 28));
    vv_checkbox(c, "sub", "Subtract?", s->subtract, MSG_TOGGLE);
    vv_text(c, vv_fmt(c, "%lld", (long long)s->count),
            VV_STYLE(.fg = t->accent_hi, .font_size = 44));
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
      vv_button(c, "a", s->subtract ? "-1" : "+1", MSG_STEP, vv_pi(step));
      vv_button(c, "b", s->subtract ? "-10" : "+10", MSG_STEP, vv_pi(step * 10));
      vv_button(c, "r", "Reset", MSG_RESET, VV_NO_PAYLOAD);
    }
  }
}
