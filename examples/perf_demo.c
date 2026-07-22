// perf_demo.c — dropping the performance HUD (vv_perf_hud.h) onto a deliberately
// heavy dashboard layout. The dashboard below is an ordinary Verve app: a header
// bar, a sidebar, a wall of metric cards, and a long data table — hundreds of
// nodes, so layout/present actually cost something worth looking at. Nothing in
// app_view/app_update knows the HUD exists; it is bolted on with three calls.
//
//   press F11   -> toggle the performance overlay
//   the Timeline tab graphs frame time + per-phase cost over the last 128 frames
//
// Build:  make VV_PERF=1 perf_demo    (VV_PERF instruments the core + this file)
//   then  ./build/perf_demo
// Building without VV_PERF still runs — the panel just says how to turn it on.
#include "verve/verve.h"
#include "vv_sdl_gl.h"

#include "vv_perf_hud.h"  // implementation ships in the GL backend

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

// -------- an ordinary (but heavy) app: nothing here is HUD-aware --------
enum { A_NAV = 1, A_LOAD, A_ROWS };
typedef struct {
  int  nav;        // selected sidebar entry
  float load;      // synthetic "load" slider, drives the sparkline heights
  int  rows;       // how many table rows to build (stress knob)
  float phase;     // animation phase for the little bars
} AppState;

static void app_update(void *st, vv_Event ev) {
  AppState *s = st;
  if (ev.msg == A_NAV)  s->nav = (int)ev.data.as_int;
  if (ev.msg == A_LOAD) s->load = (float)ev.data.as_float;
  if (ev.msg == A_ROWS) s->rows = (int)ev.data.as_float;
}

static const char *NAV[] = {"Overview", "Traffic", "Revenue", "Users", "Logs"};

static void metric_card(vv_Ctx *c, int i, const AppState *s) {
  const vv_Theme *t = vv_theme();
  char k[16]; snprintf(k, sizeof k, "card%d", i);
  vv_box_keyed(c, k, strlen(k),
               VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(190), .gap = 8,
                         .padding = vv_all(14)),
               VV_STYLE(.bg = t->surface, .radius = vv_r(12),
                        .border_width = vv_all(1), .border_color = t->border));
  {
    vv_text(c, vv_fmt(c, "Metric %02d", i + 1),
            VV_STYLE(.fg = t->text_muted, .font_size = 12));
    vv_text(c, vv_fmt(c, "%.1f%%", 40.0f + 55.0f * s->load * (0.4f + 0.6f * ((i * 7) % 11) / 11.0f)),
            VV_STYLE(.fg = t->text, .font_size = 26));
    // a little inline bar chart built from boxes
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .h = vv_fixed(34), .gap = 3,
                        .cross = VV_ALIGN_END),
           VV_STYLE(.bg = {0})) {
      for (int b = 0; b < 12; b++) {
        float ph = s->phase + (float)(i * 12 + b) * 0.4f;
        float hgt = 6.0f + 26.0f * (0.5f + 0.5f * sinf(ph)) * (0.3f + 0.7f * s->load);
        char bk[8]; snprintf(bk, sizeof bk, "b%d", b);
        vv_box_keyed(c, bk, strlen(bk),
                     VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(hgt)),
                     VV_STYLE(.bg = vv_color_lerp(t->accent_lo, t->accent_hi,
                                                  (float)b / 11.0f),
                              .radius = vv_r(3)));
        vv_end_box(c);
      }
    }
  }
  vv_end_box(c);
}

static void app_view(vv_Ctx *c, void *st) {
  AppState *s = st;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.08f, 0.09f, 0.11f))) {
    // ---- top bar ----
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(56),
                        .cross = VV_ALIGN_CENTER, .gap = 16,
                        .padding = (vv_Edges){20, 0, 20, 0}),
           VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.14f),
                    .border_width = (vv_Edges){0, 0, 0, 1}, .border_color = t->border)) {
      vv_text(c, "Verve Analytics", VV_STYLE(.fg = t->text, .font_size = 18));
      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      vv_text(c, vv_fmt(c, "%d rows", s->rows),
              VV_STYLE(.fg = t->text_muted, .font_size = 13));
    }

    // ---- body: sidebar + main ----
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)),
           VV_STYLE(.bg = {0})) {
      // sidebar
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(180), .h = vv_grow(1),
                          .gap = 6, .padding = vv_all(14)),
             VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.13f),
                      .border_width = (vv_Edges){0, 0, 1, 0}, .border_color = t->border)) {
        for (int i = 0; i < 5; i++)
          vv_list_item(c, NAV[i], NAV[i], s->nav == i, A_NAV, vv_pi(i));
        VV_BOX(c, VV_LAYOUT(.h = vv_grow(1)), VV_STYLE(.bg = {0})) {}
        vv_text(c, "Load", VV_STYLE(.fg = t->text_muted, .font_size = 12));
        vv_slider(c, "load", s->load, 0.0f, 1.0f, A_LOAD);
        vv_text(c, "Table rows", VV_STYLE(.fg = t->text_muted, .font_size = 12));
        vv_slider(c, "rows", (float)s->rows, 5.0f, 120.0f, A_ROWS);
      }

      // main scroll area
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                          .scroll_y = true, .clip = true, .gap = 18,
                          .padding = vv_all(20)),
             VV_STYLE(.bg = {0})) {
        vv_text(c, vv_fmt(c, "%s", NAV[s->nav]),
                VV_STYLE(.fg = t->text, .font_size = 22));

        // wall of metric cards (wraps)
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 14,
                            .wrap = true),
               VV_STYLE(.bg = {0})) {
          for (int i = 0; i < 8; i++) metric_card(c, i, s);
        }

        // a long data table (the real node-count stress)
        vv_text(c, "Transactions", VV_STYLE(.fg = t->text, .font_size = 16));
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 1,
                            .padding = vv_all(2)),
               VV_STYLE(.bg = t->surface, .radius = vv_r(10),
                        .border_width = vv_all(1), .border_color = t->border)) {
          for (int r = 0; r < s->rows; r++) {
            char rk[16]; snprintf(rk, sizeof rk, "row%d", r);
            bool alt = (r & 1) != 0;
            vv_box_keyed(c, rk, strlen(rk),
                         VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 12,
                                   .cross = VV_ALIGN_CENTER,
                                   .padding = (vv_Edges){12, 7, 12, 7}),
                         VV_STYLE(.bg = alt ? vv_rgb(0.11f, 0.12f, 0.15f) : (vv_Color){0},
                                  .radius = vv_r(6)));
            {
              vv_box_keyed(c, "id", 2, VV_LAYOUT(.w = vv_fixed(60)), VV_STYLE(.bg = {0}));
              vv_text(c, vv_fmt(c, "#%04d", 1000 + r),
                      VV_STYLE(.fg = t->text_muted, .font_size = 13));
              vv_end_box(c);
              vv_box_keyed(c, "nm", 2, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0}));
              vv_text(c, vv_fmt(c, "Account %c%d", 'A' + (r % 26), r * 3 % 97),
                      VV_STYLE(.fg = t->text, .font_size = 13));
              vv_end_box(c);
              // status pill
              bool ok = (r * 7 % 5) != 0;
              vv_box_keyed(c, "st", 2,
                           VV_LAYOUT(.padding = (vv_Edges){10, 3, 10, 3}),
                           VV_STYLE(.bg = ok ? vv_rgba(0.38f, 0.86f, 0.60f, 0.18f)
                                             : vv_rgba(0.95f, 0.55f, 0.42f, 0.18f),
                                    .radius = vv_r(10)));
              vv_text(c, ok ? "settled" : "pending",
                      VV_STYLE(.fg = ok ? vv_rgb(0.5f, 0.9f, 0.68f)
                                        : vv_rgb(0.98f, 0.68f, 0.55f),
                               .font_size = 11));
              vv_end_box(c);
              vv_box_keyed(c, "amt", 3, VV_LAYOUT(.w = vv_fixed(90)), VV_STYLE(.bg = {0}));
              vv_text(c, vv_fmt(c, "$%d.%02d", 10 + r * 13 % 900, r * 37 % 100),
                      VV_STYLE(.fg = t->text, .font_size = 13));
              vv_end_box(c);
            }
            vv_end_box(c);
          }
        }
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Perf HUD", 1280, 800);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  VV_PERF_INIT(&ctx);              // turn on the core's instrumentation

  // --- attach the HUD ---
  vv_PerfHud hud;
  vv_perf_hud_init(&hud, &ctx, vv_app_measure, app);

  AppState state = {.nav = 0, .load = 0.6f, .rows = 40, .phase = 0};
  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  bool f11_prev = false;
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;
    state.phase += dt * 2.0f;

    const bool *ks = SDL_GetKeyboardState(NULL);
    bool f11 = ks[SDL_SCANCODE_F11];
    if (f11 && !f11_prev) vv_perf_hud_toggle(&hud);
    f11_prev = f11;

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    // --- three lines of integration ---
    vv_Input app_in = vv_perf_hud_split(&hud, in, (float)w, (float)h);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &app_in, app_update, app_view, &state);
    vv_CommandBuffer *ov = vv_perf_hud_render(&hud, dt, (float)w, (float)h, dpi);

    vv_app_frame_begin(app, vv_rgb(0.07f, 0.08f, 0.10f));
    if (cmds) vv_render(vv_app_backend(app), cmds, w, h, dpi);
    if (ov)   vv_render(vv_app_backend(app), ov, w, h, dpi);
    vv_app_frame_end(app);
  }
  VV_PERF_PRINT(&ctx);             // also dump a text report on exit
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
