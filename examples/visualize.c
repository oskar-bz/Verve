// visualize.c — the visualizer widgets (§14.5) in one small signal dashboard.
//
// One model, one update(), pure views — the same split as every other Verve app,
// now driving vector graphics. An xy_pad sets the source wave's frequency and
// amplitude; a curve_editor shapes a transfer function; a live plot shows the
// raw wave and the shaped result. All three lean on the vv_draw_* canvas, which
// lowers to the VV_CMD_POLY primitive the SDL/GL backend renders.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <math.h>

static const vv_Theme *TH;

enum { MSG_KNOB = 1, MSG_CURVE };

#define NSAMP 256
#define NCURVE 5

typedef struct {
  vv_Vec2 knob;            // x = frequency, y = amplitude (both 0..1)
  vv_Vec2 curve[NCURVE];   // transfer function; x fixed, y editable (0..1)
  float   raw[NSAMP];      // source wave, normalized 0..1
  float   shaped[NSAMP];   // raw run through the transfer curve, scaled by amp
  double  t;               // animation phase (makes the wave scroll)
} App;

// Piecewise-linear transfer curve evaluated at u in [0,1].
static float apply_curve(const App *s, float u) {
  u = vv_clampf(u, 0.0f, 1.0f);
  for (int i = 0; i < NCURVE - 1; i++) {
    if (u <= s->curve[i + 1].x) {
      float span = s->curve[i + 1].x - s->curve[i].x;
      float k = span > 1e-5f ? (u - s->curve[i].x) / span : 0.0f;
      return vv_lerpf(s->curve[i].y, s->curve[i + 1].y, k);
    }
  }
  return s->curve[NCURVE - 1].y;
}

static void recompute(App *s) {
  float freq = 1.0f + s->knob.x * 6.0f;   // 1..7 cycles across the window
  float amp  = 0.15f + s->knob.y * 0.85f;
  for (int i = 0; i < NSAMP; i++) {
    float ph = (float)i / NSAMP;
    float base = sinf(ph * freq * 6.2831853f + (float)s->t * 2.0f);
    float u = (base + 1.0f) * 0.5f;
    s->raw[i] = u;
    s->shaped[i] = apply_curve(s, u) * amp;
  }
}

static void update(void *st, vv_Event ev) {
  App *s = st;
  switch (ev.msg) {
  case MSG_KNOB:
    s->knob = vv_as_v2(ev.data);
    break;
  case MSG_CURVE: {
    const vv_CurveEdit *e = vv_as_curve_edit(ev.data);
    if (e && e->index >= 0 && e->index < NCURVE)
      s->curve[e->index].y = vv_clampf(e->pos.y, 0.0f, 1.0f); // x stays fixed
  } break;
  }
  recompute(s);
}

static void panel(vv_Ctx *c, const char *title) {
  vv_text(c, title, VV_STYLE(.fg = TH->text_muted, .font_size = TH->font_size - 2));
}

static void view(vv_Ctx *c, void *st) {
  App *s = st;
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(16), .gap = 12),
         VV_STYLE(.bg = vv_rgb(0.07f, 0.08f, 0.10f))) {
    vv_text(c, "Signal Studio", VV_STYLE(.fg = TH->text, .font_size = TH->font_size + 8));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1), .gap = 14),
           VV_STYLE(.bg = {0})) {

      // Left column: the two interactive controls.
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(240), .h = vv_grow(1), .gap = 10),
             VV_STYLE(.bg = {0})) {
        panel(c, "Source  (drag: x=frequency, y=amplitude)");
        vv_xy_pad(c, "knob", s->knob, MSG_KNOB);
        panel(c, "Transfer curve  (drag the points)");
        vv_curve_editor(c, "curve", s->curve, NCURVE, MSG_CURVE);
      }

      // Right: the live plot of raw vs. shaped signal.
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 6),
             VV_STYLE(.bg = {0})) {
        panel(c, "Output  (muted = source, accent = shaped)");
        vv_PlotSeries series[2] = {
          {.ys = s->raw,    .count = NSAMP, .color = TH->text_muted,
           .kind = VV_PLOT_LINE, .width = 1.0f},
          {.ys = s->shaped, .count = NSAMP, .color = TH->accent,
           .kind = VV_PLOT_LINE, .width = 2.5f},
        };
        vv_plot(c, "plot", series, 2,
                (vv_PlotOpts){.y_min = 0.0f, .y_max = 1.0f, .auto_x = true,
                              .grid = true, .height = 0});
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Signal Studio", 980, 640);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  TH = vv_theme();

  App state = {0};
  state.knob = vv_v2(0.35f, 0.7f);
  for (int i = 0; i < NCURVE; i++)
    state.curve[i] = vv_v2((float)i / (NCURVE - 1), (float)i / (NCURVE - 1)); // identity
  recompute(&state);

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;

    // Advance the live wave, then force a rebuild so the plot keeps scrolling.
    state.t += dt;
    recompute(&state);
    vv_invalidate(&ctx);

    int w, h; float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.07f, 0.08f, 0.10f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }

  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
