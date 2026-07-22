// palette.c — a *perceptual* color studio built on Verve's OKLab core. You edit
// a color in OKLCH (lightness / chroma / hue) — the space where equal steps
// look equal — and Verve generates a tint/shade ramp that's evenly spaced to
// the eye, not to the sRGB cube. Live WCAG contrast tells you if text will be
// readable.
//
// Why it's a good Verve demo: the big preview and every ramp swatch are just
// style *targets*. Nudge a slider and they don't jump — they spring to the new
// color *through OKLab* (vv_color_lerp), so even a blue->yellow move stays
// vivid instead of sliding through grey. That perceptual interpolation is the
// same machinery the whole library animates color with.
//
//   drag L / C / H   -> preview + ramp glide to the new color
//   click a swatch   -> adopt it as the new base
// Build with `make gui`, run ./build/palette.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

#define NRAMP 9
#define DEG (3.14159265f / 180.0f)

enum { MSG_L = 1, MSG_C, MSG_H, MSG_PICK };

// OKLCH is OKLab in polar form: chroma + hue instead of the a/b axes. It's the
// intuitive knob set — L is perceived lightness, C is colorfulness, H is the
// hue angle — so a slider on each does what you expect.
typedef struct {
  float L, C, H;
} State;

static vv_Color oklch(float L, float C, float H) {
  vv_Oklab o = {L, C * cosf(H * DEG), C * sinf(H * DEG), 1.0f};
  return vv_oklab_to_srgb(o);
}

static float lin(float c) {
  return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
static float rel_lum(vv_Color c) {
  return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}
static float contrast(vv_Color a, vv_Color b) {
  float la = rel_lum(a) + 0.05f, lb = rel_lum(b) + 0.05f;
  return la > lb ? la / lb : lb / la;
}

static void update(void *state, vv_Event ev) {
  State *s = state;
  switch (ev.msg) {
  case MSG_L:
    s->L = (float)ev.data.as_float;
    break;
  case MSG_C:
    s->C = (float)ev.data.as_float;
    break;
  case MSG_H:
    s->H = (float)ev.data.as_float;
    break;
  case MSG_PICK:
    s->L = (float)ev.data.as_float;
    break; // adopt a ramp step's L
  default:
    break;
  }
}

// A labelled slider row: caption + value readout + track.
static void knob(vv_Ctx *c, const char *key, const char *name, float val,
                 float lo, float hi, const char *unit, vv_Msg msg) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 4),
         VV_STYLE(.bg = {0})) {
    VV_BOX(c,
           VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                     .main = VV_ALIGN_SPACE_BETWEEN),
           VV_STYLE(.bg = {0})) {
      vv_text(c, name, VV_STYLE(.fg = t->text, .font_size = 14));
      vv_text(c, vv_fmt(c, "%.3g%s", (double)val, unit),
              VV_STYLE(.fg = t->text_muted, .font_size = 14));
    }
    vv_slider(c, key, val, lo, hi, msg);
  }
}

// A WCAG contrast badge: the ratio of `fg` on `bg`, tinted by whether it
// passes.
static void badge(vv_Ctx *c, const char *key, const char *label, vv_Color fg,
                  vv_Color bg) {
  const vv_Theme *t = vv_theme();
  float ratio = contrast(fg, bg);
  bool aa = ratio >= 4.5f, aaa = ratio >= 7.0f;
  vv_Color mark = aa ? vv_rgb(0.35f, 0.75f, 0.4f) : vv_rgb(0.85f, 0.4f, 0.35f);
  VV_BOX(c,
         VV_LAYOUT(.dir = VV_ROW, .cross = VV_ALIGN_CENTER, .gap = 8,
                   .padding = vv_hv(12, 8), .h=vv_grow(1)),
         VV_STYLE(.bg = bg, .radius = vv_r(8), .border_width = vv_all(1),
                  .border_color = t->border)) {
    vv_text(c, label, VV_STYLE(.fg = fg, .font_size = 15));
    vv_text(c,
            vv_fmt(c, "%.1f:1  %s", (double)ratio,
                   aaa  ? "AAA"
                   : aa ? "AA"
                        : "fail"),
            VV_STYLE(.fg = mark, .font_size = 13));
  }
  (void)key;
}

static void view(vv_Ctx *c, void *state) {
  State *s = state;
  const vv_Theme *t = vv_theme();
  vv_Color base = oklch(s->L, s->C, s->H);
  int R = (int)(base.r * 255 + 0.5f), G = (int)(base.g * 255 + 0.5f),
      B = (int)(base.b * 255 + 0.5f);

  VV_BOX(c,
         VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                   .padding = vv_all(24), .gap = 18),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {

    vv_text(c, "OKLab Palette Studio",
            VV_STYLE(.fg = t->text, .font_size = 24));
    vv_text(c, "perceptual color · the ramp is evenly spaced to the eye",
            VV_STYLE(.fg = t->text_muted, .font_size = 13));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 20),
           VV_STYLE(.bg = {0})) {

      // Preview: a big swatch (springs through OKLab on edit) + readouts.
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 12),
             VV_STYLE(.bg = {0})) {
        VV_BOX(c,
               VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(200),
                         .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = base, .radius = vv_r(14),
                        .shadow = {.color = vv_rgba(0, 0, 0, 0.4f),
                                   .offset = {0, 10},
                                   .blur = 30})) {
          // legible label picks black or white by contrast
          vv_Color ink =
              contrast(base, vv_rgb(1, 1, 1)) >= contrast(base, vv_rgb(0, 0, 0))
                  ? vv_rgb(1, 1, 1)
                  : vv_rgb(0, 0, 0);
          vv_text(c, vv_fmt(c, "#%02X%02X%02X", R, G, B),
                  VV_STYLE(.fg = ink, .font_size = 30));
        }
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
          badge(c, "bw", "on white", base, vv_rgb(1, 1, 1));
          badge(c, "bb", "on black", vv_rgb(1, 1, 1), base);
        }
      }

      // Controls.
      VV_BOX(c,
             VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(340), .gap = 18,
                       .padding = vv_all(18)),
             VV_STYLE(.bg = vv_rgb(0.12f, 0.13f, 0.16f), .radius = vv_r(12))) {
        knob(c, "L", "Lightness", s->L, 0.0f, 1.0f, "", MSG_L);
        knob(c, "C", "Chroma", s->C, 0.0f, 0.37f, "", MSG_C);
        knob(c, "H", "Hue", s->H, 0.0f, 360.0f, "\xc2\xb0", MSG_H);
      }
    }

    // Perceptual ramp: same C+H, L swept evenly. Click one to adopt its L.
    VV_BOX(
        c,
        VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(72), .gap = 8),
        VV_STYLE(.bg = {0})) {
      for (int i = 0; i < NRAMP; i++) {
        float L = vv_lerpf(0.18f, 0.95f, (float)i / (NRAMP - 1));
        vv_Color sw = oklch(L, s->C, s->H);
        char key[8];
        snprintf(key, sizeof key, "sw%d", i);
        vv_Style hov = {.transform = vv_scale(1.05f),
                        .set = VV_STYLE_TRANSFORM};
        uint32_t id = vv_box_keyed(
            c, key, strlen(key), VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1)),
            VV_STYLE(.bg = sw, .radius = vv_r(10), .hover = &hov));
        vv_end_box(c);
        if (vv_clicked(c, id))
          vv_emit(c, MSG_PICK, vv_pf((double)L));
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Palette", 980, 640);
  if (!app)
    return 1;
  const char *fonts[] = {
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++)
    if (vv_app_load_font(app, fonts[i]))
      break;

  vv_Ctx ctx;
  vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  State state = {.L = 0.62f, .C = 0.17f, .H = 255.0f}; // a nice blue
  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;
    if (dt > 0.1f)
      dt = 0.1f;

    int w, h;
    float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.09f, 0.10f, 0.12f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
