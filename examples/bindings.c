// bindings.c — the smallest demo of two-way value bindings (§6).
//
// The point: these controls edit plain variables directly. There is no update()
// case for them — a `_bound` widget reads the variable to render and, on change,
// emits the reserved VV_MSG_BIND, which the driver applies through the pointer
// *before* update() runs. So `update` here is empty; the labels below the knobs
// re-read the same variables and stay in lockstep, proving the round-trip.
//
// Compare with mycounter.c, which routes every change through a message. Use
// bindings for plain "edit this number" knobs; use messages when a change has
// consequences (recompute, network, navigation).
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>

static const vv_Theme *TH;

// The model is just variables — no message enum, no dispatch.
typedef struct {
  float gain;
  int32_t voices;
  bool mute;
} Synth;

// Metadata gives bound widgets their range and feel. `curve > 1` skews a slider
// so there's finer control near the minimum (handy for gain/frequency).
static const vv_ValueMeta GAIN_META   = {.name = "Gain", .min = 0, .max = 1, .curve = 2.0f};
static const vv_ValueMeta VOICES_META = {.name = "Voices", .min = 1, .max = 16};

// No bound-widget cases to handle: the driver applies VV_MSG_BIND itself.
static void update(void *state, vv_Event ev) {
  (void)state; (void)ev;
}

static void row(vv_Ctx *c, const char *label, vv_Str value) {
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .main = VV_ALIGN_SPACE_BETWEEN),
         VV_STYLE(.bg = {0})) {
    vv_text(c, label, VV_STYLE(.fg = TH->text_muted, .font_size = 14));
    vv_text(c, value, VV_STYLE(.fg = TH->text, .font_size = 14));
  }
}

static void view(vv_Ctx *c, void *state) {
  Synth *s = state;
  VV_BOX(c,
         VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                   .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 16),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.09f, 0.11f))) {

    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(320), .gap = 14,
                        .padding = vv_all(24)),
           VV_STYLE(.bg = vv_rgb(0.13f, 0.14f, 0.17f), .radius = vv_r(12))) {
      vv_text(c, "Value bindings", VV_STYLE(.fg = TH->text, .font_size = 22));
      vv_text(c, "controls edit the variables directly",
              VV_STYLE(.fg = TH->text_muted, .font_size = 13));

      // Each _bound widget takes a vv_Value wrapping the variable + its meta.
      vv_slider_bound(c, "gain", vv_f32(&s->gain, &GAIN_META));
      vv_drag_number_bound(c, "voices", vv_i32(&s->voices, &VOICES_META), 0.05f);
      vv_checkbox_bound(c, "mute", "Mute", vv_boolval(&s->mute, NULL));

      // These labels re-read the same variables — no separate copy of state.
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 6,
                          .padding = (vv_Edges){0, 12, 0, 0}),
             VV_STYLE(.bg = {0})) {
        row(c, "gain",   vv_fmt(c, "%.3f", (double)s->gain));
        row(c, "voices", vv_fmt(c, "%d", s->voices));
        row(c, "mute",   vv_fmt(c, "%s", s->mute ? "on" : "off"));
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Value Bindings", 640, 520);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);
  TH = vv_theme();

  Synth state = {.gain = 0.5f, .voices = 4, .mute = false};

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;

    int w, h; float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.09f, 0.09f, 0.11f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    } else {
      vv_app_wait_event(app, 16); // idle: sleep instead of busy-spinning
    }
  }

  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
