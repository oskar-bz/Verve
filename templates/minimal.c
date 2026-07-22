// minimal.c — the smallest complete Verve app. Copy this file, rename the
// messages/state, and grow the view. The whole window + event loop is one call
// (vv_app_run); you only write update() and view().
//
//   Build:  make template-minimal   ->   ./build/tpl_minimal
//
// The two-function shape is the core idea:
//   • view()   is a pure function of state: it builds the UI and never mutates
//     state. Widgets emit *messages* when acted on.
//   • update() is the only place state changes: it receives each message.
// vv_run_frame drains messages into update(), then rebuilds view() only when
// something changed (idle frames just present, so animations still advance).
#include "vv_sdl_gl.h"

// Messages — 0 is reserved (VV_MSG_NONE), so start at 1.
enum { MSG_INC = 1, MSG_RESET };

typedef struct {
  int count;
} App;

static void update(void *state, vv_Event ev) {
  App *a = state;
  switch (ev.msg) {
  case MSG_INC:   a->count += (int)ev.data.as_int; break;
  case MSG_RESET: a->count = 0;                    break;
  default: break;
  }
}

static void view(vv_Ctx *c, void *state) {
  App *a = state;
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 14),
         VV_STYLE(.bg = t->surface_app)) {
    vv_text(c, "Verve", VV_STYLE(.fg = t->text_primary, .font_size = 28));
    vv_text(c, vv_fmt(c, "%d", a->count),
            VV_STYLE(.fg = t->brand_primary, .font_size = 44));
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
      vv_button(c, "inc",   "+1",    MSG_INC, vv_pi(1));
      vv_button(c, "inc10", "+10",   MSG_INC, vv_pi(10));
      vv_button(c, "reset", "Reset", MSG_RESET, VV_NO_PAYLOAD);
    }
  }
}

int main(void) {
  App state = {0};
  return vv_app_run(&(vv_AppDesc){
      .title  = "Verve \xc2\xb7 Minimal",
      .width  = 720,
      .height = 480,
      .update = update,
      .view   = view,
      .state  = &state,
  });
}
