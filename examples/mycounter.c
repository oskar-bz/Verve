// mycounter.c — the reference message/update/view app on Verve.
//
// Application logic (update) and view logic (view) are fully separated: view()
// never mutates state, update() never touches the UI. Widgets emit messages;
// vv_run_frame drains them into update() and rebuilds view() only when
// something changed (idle frames just present, advancing animations).
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>

static const vv_Theme *TH;

// ---- messages -------------------------------------------------------------
// 0 is reserved (VV_MSG_NONE), so start at 1.
enum {
  MSG_STEP = 1,   // add ±1 (payload carries the signed amount)
  MSG_TOGGLE_SUB, // flip subtract mode (payload = new bool)
  MSG_RESET,
};

// ---- state ----------------------------------------------------------------
typedef struct {
  bool    subtract;
  int64_t counter;
} Counter;

// ---- update: the only place state changes --------------------------------
static void update(void *state, vv_Event ev) {
  Counter *s = state;
  switch (ev.msg) {
  case MSG_STEP:       s->counter += ev.data.as_int; break;
  case MSG_TOGGLE_SUB: s->subtract = ev.data.as_int;  break;
  case MSG_RESET:      s->counter = 0;                break;
  }
}

// ---- view: pure function of state ----------------------------------------
static void view(vv_Ctx *c, void *state) {
  Counter *s = state;
  int64_t step = s->subtract ? -1 : 1;
  VV_BOX(c,
         VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                   .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 12),
         VV_STYLE(.bg = vv_rgb(24.f / 255, 24.f / 255, 24.f / 255))) {
    vv_text(c, "Counter!", VV_STYLE(.fg = TH->text, .font_size = 26));

    vv_checkbox(c, "sub", "Enable subtraction?", s->subtract, MSG_TOGGLE_SUB);

    vv_text(c, vv_fmt(c, "%lld", (long long)s->counter),
            VV_STYLE(.fg = TH->text, .font_size = 40));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
      vv_button(c, "step1", s->subtract ? "-1" : "+1", MSG_STEP, vv_pi(step));
      vv_button(c, "step10", s->subtract ? "-10" : "+10", MSG_STEP, vv_pi(step * 10));
      vv_button(c, "reset", "Reset", MSG_RESET, VV_NO_PAYLOAD);
    }
  }
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;

  vv_App *app = vv_app_create("Verve - My Counter", 900, 640);
  if (!app) return 1;

  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  TH = vv_theme();

  Counter state = {0};

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;

    int w, h; float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    // The whole loop: drain messages -> update -> conditionally rebuild view.
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);

    vv_app_frame_begin(app, vv_rgb(24.f / 255, 24.f / 255, 24.f / 255));
    vv_render(vv_app_backend(app), cmds, w, h, dpi);
    vv_app_frame_end(app);
  }

  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
