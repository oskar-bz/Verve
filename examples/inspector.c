// inspector.c — demo of attaching the drop-in inspector (vv_inspect.h) to an
// ordinary Verve app. The app below is written exactly as any app would be; the
// inspector is bolted on with three calls in the loop and an F12 toggle. Nothing
// in app_view/app_update knows the inspector exists.
//
//   press F12       -> toggle the inspector overlay
//   hover the app   -> outline the node under the cursor
//   click the app    -> pin that node in the panel  (click a row to pin instead)
// Build with `make gui`, run ./build/inspector.
#include "verve/verve.h"
#include "vv_sdl_gl.h"

#include "vv_inspect.h"  // implementation ships in the GL backend

#include <SDL3/SDL.h>
#include <stdio.h>

// -------- an ordinary app: nothing here is inspector-aware --------
enum { A_STEP = 1, A_SLIDE };
typedef struct { int count; float amt; } AppState;

static void app_update(void *st, vv_Event ev) {
  AppState *s = st;
  if (ev.msg == A_STEP)  s->count += (int)ev.data.as_int;
  if (ev.msg == A_SLIDE) s->amt = (float)ev.data.as_float;
}

static void app_view(vv_Ctx *c, void *st) {
  AppState *s = st;
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(24), .gap = 16),
         VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.14f))) {
    vv_text(c, "Sample App", VV_STYLE(.fg = t->text, .font_size = 22));
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 12,
                        .padding = vv_all(18)),
           VV_STYLE(.bg = t->surface, .radius = vv_r(12),
                    .border_width = vv_all(1), .border_color = t->border)) {
      vv_text(c, "A little tree to poke at", VV_STYLE(.fg = t->text_muted, .font_size = 14));
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER),
             VV_STYLE(.bg = {0})) {
        vv_button(c, "dec", "-1", A_STEP, vv_pi(-1));
        vv_text(c, vv_fmt(c, "%d", s->count), VV_STYLE(.fg = t->text, .font_size = 28));
        vv_button(c, "inc", "+1", A_STEP, vv_pi(1));
      }
      vv_slider(c, "amt", s->amt, 0.0f, 1.0f, A_SLIDE);
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 8), VV_STYLE(.bg = {0})) {
        for (int i = 0; i < 3; i++) {
          char k[4]; snprintf(k, sizeof k, "s%d", i);
          vv_box_keyed(c, k, 2, VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(40)),
                       VV_STYLE(.bg = vv_color_lerp(t->accent_lo, t->accent_hi,
                                                    (float)i / 2 * s->amt),
                                .radius = vv_r(8)));
          vv_end_box(c);
        }
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Inspector", 1100, 680);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);

  // --- attach the inspector ---
  vv_Inspector ins;
  vv_inspect_init(&ins, &ctx, vv_app_measure, app);

  AppState state = {0};
  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  bool f12_prev = false;
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    // Toggle on F12. vv_Input doesn't carry F-keys, so read the backend's
    // keyboard directly — every backend exposes some equivalent.
    const bool *ks = SDL_GetKeyboardState(NULL);
    bool f12 = ks[SDL_SCANCODE_F12];
    if (f12 && !f12_prev) vv_inspect_toggle(&ins);
    f12_prev = f12;

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    // --- three lines of integration ---
    vv_Input app_in = vv_inspect_split(&ins, in, (float)w, (float)h);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &app_in, app_update, app_view, &state);
    vv_CommandBuffer *ov = vv_inspect_render(&ins, dt, (float)w, (float)h, dpi);

    vv_app_frame_begin(app, vv_rgb(0.07f, 0.08f, 0.10f));
    if (cmds) vv_render(vv_app_backend(app), cmds, w, h, dpi);
    if (ov)   vv_render(vv_app_backend(app), ov, w, h, dpi);
    vv_app_frame_end(app);
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
