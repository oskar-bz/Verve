// vv_perf_hud.h — a drop-in performance overlay for any Verve app.
//
// A little DevTools-style "FPS/frame" panel that reads the granular timing the
// core records under -DVV_PERF (see vv_perf.h) and paints it as a floating card:
// live frame time + FPS, a per-phase cost breakdown, and scrolling graphs of
// where the frame budget goes over time.
//
// Single header. In exactly one .c file define the implementation:
//     #define VV_PERF_HUD_IMPL
//     #include "vv_perf_hud.h"
// elsewhere just #include it. Depends only on the Verve core (verve.h), not on
// any backend, so it attaches to whatever window/loop you already have.
//
// ------------------------------------------------------------------ attaching
//   #define VV_PERF_HUD_IMPL
//   #include "vv_perf_hud.h"
//   ...
//   vv_Ctx ctx; vv_init(&ctx);
//   vv_set_measure_fn(&ctx, my_measure, my_ud);
//   VV_PERF_INIT(&ctx);                 // start the core's instrumentation
//
//   vv_PerfHud hud;
//   vv_perf_hud_init(&hud, &ctx, my_measure, my_ud);
//
//   // per frame (w,h,dpi from your window; `in` is your vv_Input):
//   vv_Input app_in = vv_perf_hud_split(&hud, in, w, h); // routes the pointer
//   vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &app_in, update, view, &st);
//   vv_CommandBuffer *ov   = vv_perf_hud_render(&hud, dt, w, h, dpi);
//
//   frame_begin(clear);
//   if (cmds) render(cmds);   // your app
//   if (ov)   render(ov);     // the HUD, on top
//   frame_end();
//
//   // toggle however your backend exposes keys, e.g. on F11:
//   vv_perf_hud_toggle(&hud);
//
// NOTE: the numbers are zero unless BOTH the core library and this translation
// unit are built with -DVV_PERF (e.g. `make VV_PERF=1 ...`). Built without it
// the panel still attaches — it just tells you to turn instrumentation on.
#ifndef VV_PERF_HUD_H
#define VV_PERF_HUD_H

#include "verve/verve.h"
#include "verve/vv_perf.h"

#ifndef VV_MEASUREFN_DEFINED
#define VV_MEASUREFN_DEFINED
typedef vv_Vec2 (*vv_MeasureFn)(void *ud, const char *s, int len, vv_FontID font,
                                float size, float wrap_width);
#endif

enum { VV_PERF_HUD_HISTORY = 128 };

typedef struct {
  vv_Ctx  ctx;         // the HUD's own context (separate tree)
  vv_Ctx *target;      // the app whose timings we read
  bool    open;
  bool    windowed;    // rendering into its own native window (fills it)
  int     tab;         // 0 = overview, 1 = phases, 2 = timeline
  float   panel_w;
  float   x, y;        // top-left of the card (window coords)

  // Per-frame instantaneous cost of each phase (ms), lightly smoothed.
  float    ms[VV_PERF_COUNT];
  // Baseline cumulative counters, to turn accumulation into per-frame deltas.
  uint64_t last_ns[VV_PERF_COUNT];
  uint64_t last_frames[VV_PERF_COUNT];
  bool     primed;

  // Scrolling history (chronological; index 0 is oldest) for the timeline tab.
  float hist_frame[VV_PERF_HUD_HISTORY];
  float hist_input[VV_PERF_HUD_HISTORY];
  float hist_layout[VV_PERF_HUD_HISTORY];
  float hist_present[VV_PERF_HUD_HISTORY];
  float hist_budget[VV_PERF_HUD_HISTORY]; // flat 16.6ms reference line
  int   filled;

  float fps;           // wall-clock FPS (independent of build/present tiers)

  // scratch carried between split() and render() within a frame
  vv_Input ui_in;
  bool     over_panel;
} vv_PerfHud;

// Attach to `target`. `measure`/`ud` are the same text-measurement callback you
// gave the app (the HUD lays out its own text with it).
void vv_perf_hud_init(vv_PerfHud *hud, vv_Ctx *target, vv_MeasureFn measure,
                      void *ud);

void vv_perf_hud_toggle(vv_PerfHud *hud);

// Call once per frame BEFORE running your app. Splits the pointer so clicks that
// land on the card drive the HUD instead of the app, and returns the vv_Input
// your app should use.
vv_Input vv_perf_hud_split(vv_PerfHud *hud, vv_Input raw, float w, float h);

// Call AFTER running your app's frame. Samples the target's timers, builds the
// overlay and returns its command buffer (NULL when closed). Render it on top.
vv_CommandBuffer *vv_perf_hud_render(vv_PerfHud *hud, float dt, float w, float h,
                                     float dpi);

// Native-window variant: render the HUD filling its own window (its own
// vv_Ctx), driven by that window's input `win_in`. `wall_dt` is the app's frame
// delta (for the FPS/graph cadence), which may differ from this window's dt.
vv_CommandBuffer *vv_perf_hud_window(vv_PerfHud *hud, float dt, float wall_dt,
                                     float w, float h, float dpi, vv_Input win_in);

#endif // VV_PERF_HUD_H

// ============================================================================
#ifdef VV_PERF_HUD_IMPL
#include <stdio.h>
#include <string.h>

// The frame budget we grade against (60 Hz). Purely cosmetic.
#define VVPH_BUDGET_MS 16.6f

// A stable colour per top-level phase, so the same phase reads the same hue in
// the bars and the graphs.
static vv_Color vvph_phase_color(vv_PerfPhaseID id) {
  switch (id) {
  case VV_PERF_INPUT:       return vv_rgb(0.36f, 0.72f, 0.95f); // blue
  case VV_PERF_BUILD_BEGIN: return vv_rgb(0.58f, 0.55f, 0.95f); // indigo
  case VV_PERF_RECONCILE:   return vv_rgb(0.85f, 0.55f, 0.92f); // violet
  case VV_PERF_LAYOUT:      return vv_rgb(0.98f, 0.72f, 0.30f); // amber
  case VV_PERF_PRESENT:     return vv_rgb(0.38f, 0.86f, 0.60f); // green
  default:                  return vv_rgb(0.60f, 0.64f, 0.70f); // grey
  }
}

static const char *vvph_short_name(vv_PerfPhaseID id) {
  switch (id) {
  case VV_PERF_FRAME_TOTAL:     return "frame";
  case VV_PERF_INPUT:           return "input";
  case VV_PERF_BUILD_BEGIN:     return "build";
  case VV_PERF_RECONCILE:       return "reconcile";
  case VV_PERF_LAYOUT:          return "layout";
  case VV_PERF_LAYOUT_P1:       return "pass1 width";
  case VV_PERF_LAYOUT_P2:       return "pass2 width";
  case VV_PERF_LAYOUT_P3:       return "pass3 height";
  case VV_PERF_LAYOUT_P4:       return "pass4 pos";
  case VV_PERF_PRESENT:         return "present";
  case VV_PERF_PRESENT_STYLE:   return "style";
  case VV_PERF_PRESENT_RECT:    return "rect (FLIP)";
  case VV_PERF_PRESENT_SCROLL:  return "scroll";
  case VV_PERF_PRESENT_EMIT:    return "emit";
  case VV_PERF_PRESENT_EXITING: return "exiting";
  default:                      return "?";
  }
}

static bool vvph_is_child(vv_PerfPhaseID id) {
  return (id >= VV_PERF_LAYOUT_P1 && id <= VV_PERF_LAYOUT_P4) ||
         (id >= VV_PERF_PRESENT_STYLE && id <= VV_PERF_PRESENT_EXITING);
}

// Pull fresh per-frame timings out of the (cumulative) target samples.
static void vvph_sample(vv_PerfHud *hud, float dt) {
  const vv_PerfCtx *p = &hud->target->perf.perf;

  // Wall-clock FPS, smoothed — honest even when the app is idle-ticking.
  if (dt > 0.0f) {
    float inst = 1.0f / dt;
    hud->fps = hud->fps <= 0.0f ? inst : hud->fps * 0.9f + inst * 0.1f;
  }

  for (int i = 0; i < VV_PERF_COUNT; i++) {
    uint64_t ns = p->samples[i].ns, fr = p->samples[i].frames;
    if (!hud->primed) { hud->last_ns[i] = ns; hud->last_frames[i] = fr; }

    uint64_t dns = ns - hud->last_ns[i];
    uint64_t dfr = fr - hud->last_frames[i];
    hud->last_ns[i] = ns;
    hud->last_frames[i] = fr;

    // Per-occurrence cost this interval; hold the last value if the phase did
    // not run (e.g. layout on a present-only frame) so the reading is stable.
    if (dfr > 0) {
      float inst = (float)dns / (float)dfr / 1e6f;
      hud->ms[i] = hud->ms[i] <= 0.0f ? inst : hud->ms[i] * 0.7f + inst * 0.3f;
    }
  }
  hud->primed = true;

  // Push one point onto each scrolling history (shift-left ring).
  int n = VV_PERF_HUD_HISTORY;
  memmove(hud->hist_frame,   hud->hist_frame + 1,   (size_t)(n - 1) * sizeof(float));
  memmove(hud->hist_input,   hud->hist_input + 1,   (size_t)(n - 1) * sizeof(float));
  memmove(hud->hist_layout,  hud->hist_layout + 1,  (size_t)(n - 1) * sizeof(float));
  memmove(hud->hist_present, hud->hist_present + 1, (size_t)(n - 1) * sizeof(float));
  hud->hist_frame[n - 1]   = hud->ms[VV_PERF_FRAME_TOTAL];
  hud->hist_input[n - 1]   = hud->ms[VV_PERF_INPUT];
  hud->hist_layout[n - 1]  = hud->ms[VV_PERF_LAYOUT];
  hud->hist_present[n - 1] = hud->ms[VV_PERF_PRESENT];
  if (hud->filled < n) hud->filled++;
}

// ---- small view helpers ----------------------------------------------------

static void vvph_stat(vv_Ctx *c, const char *label, const char *val,
                      vv_Color valc) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 2),
         VV_STYLE(.bg = {0})) {
    vv_text(c, label, VV_STYLE(.fg = t->text_muted, .font_size = 11));
    vv_text(c, val, VV_STYLE(.fg = valc, .font_size = 22));
  }
}

// One horizontal cost bar: label, a track with a coloured fill scaled to the
// frame budget, and the millisecond reading.
static void vvph_bar(vv_Ctx *c, const char *key, const char *label, float ms,
                     float peak_ms, vv_Color col, bool child) {
  const vv_Theme *t = vv_theme();
  float frac = ms / VVPH_BUDGET_MS;
  if (frac > 1.0f) frac = 1.0f;
  if (frac < 0.0f) frac = 0.0f;

  char rk[24]; snprintf(rk, sizeof rk, "b_%s", key);
  vv_box_keyed(c, rk, strlen(rk),
               VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                         .gap = 8, .padding = (vv_Edges){child ? 12.0f : 0, 1, 0, 1}),
               VV_STYLE(.bg = {0}));
  {
    vv_box_keyed(c, "l", 1, VV_LAYOUT(.w = vv_fixed(child ? 84 : 72)),
                 VV_STYLE(.bg = {0}));
    vv_text(c, label, VV_STYLE(.fg = child ? t->text_muted : t->text,
                               .font_size = child ? 11 : 12));
    vv_end_box(c);

    // track
    vv_box_keyed(c, "tk", 2,
                 VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(child ? 8 : 12),
                           .cross = VV_ALIGN_CENTER),
                 VV_STYLE(.bg = vv_rgb(0.14f, 0.15f, 0.19f), .radius = vv_r(4)));
    {
      // fill (FLIP-springs as the width tracks the value)
      vv_box_keyed(c, "fl", 2,
                   VV_LAYOUT(.w = vv_percent(frac), .h = vv_grow(1)),
                   VV_STYLE(.bg = col, .radius = vv_r(4)));
      vv_end_box(c);
    }
    vv_end_box(c);

    vv_box_keyed(c, "v", 1, VV_LAYOUT(.w = vv_fixed(52)), VV_STYLE(.bg = {0}));
    vv_text(c, vv_fmt(c, "%.2f", ms),
            VV_STYLE(.fg = t->text, .font_size = child ? 11 : 12));
    vv_end_box(c);

    if (!child) {
      vv_box_keyed(c, "pk", 2, VV_LAYOUT(.w = vv_fixed(58)), VV_STYLE(.bg = {0}));
      vv_text(c, vv_fmt(c, "\xe2\x86\x91%.1f", peak_ms),
              VV_STYLE(.fg = t->text_muted, .font_size = 10));
      vv_end_box(c);
    }
  }
  vv_end_box(c);
}

// A pill-style tab; returns true when clicked this frame.
static bool vvph_tab(vv_Ctx *c, const char *key, const char *label, bool active) {
  const vv_Theme *t = vv_theme();
  uint32_t id = vv_box_keyed(
      c, key, strlen(key),
      VV_LAYOUT(.dir = VV_ROW, .padding = (vv_Edges){12, 5, 12, 5},
                .cross = VV_ALIGN_CENTER),
      VV_STYLE(.bg = active ? t->accent : vv_rgb(0.14f, 0.15f, 0.19f),
               .radius = vv_r(6)));
  vv_text(c, label, VV_STYLE(.fg = active ? t->on_accent : t->text_muted,
                             .font_size = 12));
  vv_end_box(c);
  return vv_clicked(c, id);
}

static void vvph_overview(vv_Ctx *c, vv_PerfHud *hud) {
  const vv_Theme *t = vv_theme();
  const vv_PerfCtx *p = &hud->target->perf.perf;
  float frame_ms = hud->ms[VV_PERF_FRAME_TOTAL];

  // grade the frame against budget: green under, amber near, red over
  vv_Color grade = frame_ms < VVPH_BUDGET_MS * 0.66f ? vv_rgb(0.38f, 0.86f, 0.60f)
                 : frame_ms < VVPH_BUDGET_MS         ? vv_rgb(0.98f, 0.78f, 0.35f)
                                                     : vv_rgb(0.95f, 0.42f, 0.42f);

  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 10),
         VV_STYLE(.bg = {0})) {
    vvph_stat(c, "FRAME (CPU)", vv_fmt(c, "%.3f ms", frame_ms), grade);
    vvph_stat(c, "FPS (wall)", vv_fmt(c, "%.0f", hud->fps), t->text);
  }

  // budget meter — pipeline CPU cost against the 16.6ms frame budget
  float frac = frame_ms / VVPH_BUDGET_MS; if (frac > 1) frac = 1; if (frac < 0) frac = 0;
  vv_box_keyed(c, "bm", 2, VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(6)),
               VV_STYLE(.bg = vv_rgb(0.14f, 0.15f, 0.19f), .radius = vv_r(3)));
  {
    vv_box_keyed(c, "bmf", 3, VV_LAYOUT(.w = vv_percent(frac), .h = vv_grow(1)),
                 VV_STYLE(.bg = grade, .radius = vv_r(3)));
    vv_end_box(c);
  }
  vv_end_box(c);
  vv_text(c, vv_fmt(c, "%.1f%% of 16.6ms budget", frac * 100.0f),
          VV_STYLE(.fg = t->text_muted, .font_size = 11));

  // frame-tier mix — where run_frame actually spent its frames
  uint64_t tb = p->tier_build, tp = p->tier_present, ti = p->tier_idle;
  vv_text(c, "TIER MIX", VV_STYLE(.fg = t->accent_hi, .font_size = 11));
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 12),
         VV_STYLE(.bg = {0})) {
    vvph_stat(c, "build", vv_fmt(c, "%llu", (unsigned long long)tb),
              vvph_phase_color(VV_PERF_BUILD_BEGIN));
    vvph_stat(c, "present", vv_fmt(c, "%llu", (unsigned long long)tp),
              vvph_phase_color(VV_PERF_PRESENT));
    vvph_stat(c, "idle", vv_fmt(c, "%llu", (unsigned long long)ti), t->text_muted);
  }
}

static void vvph_phases(vv_Ctx *c, vv_PerfHud *hud) {
  const vv_Theme *t = vv_theme();
  const vv_PerfCtx *p = &hud->target->perf.perf;

  vv_text(c, "TOP-LEVEL PHASES (avg ms, bar vs 16.6ms budget)",
          VV_STYLE(.fg = t->accent_hi, .font_size = 11));
  static const vv_PerfPhaseID tops[] = {
      VV_PERF_INPUT, VV_PERF_BUILD_BEGIN, VV_PERF_RECONCILE, VV_PERF_LAYOUT,
      VV_PERF_PRESENT};
  for (unsigned i = 0; i < sizeof tops / sizeof *tops; i++) {
    vv_PerfPhaseID id = tops[i];
    float peak = (float)p->samples[id].max_ns / 1e6f;
    vvph_bar(c, vvph_short_name(id), vvph_short_name(id), hud->ms[id], peak,
             vvph_phase_color(id), false);
  }

  vv_text(c, "LAYOUT PASSES", VV_STYLE(.fg = t->accent_hi, .font_size = 11));
  for (vv_PerfPhaseID id = VV_PERF_LAYOUT_P1; id <= VV_PERF_LAYOUT_P4; id++)
    vvph_bar(c, vvph_short_name(id), vvph_short_name(id), hud->ms[id], 0,
             vvph_phase_color(VV_PERF_LAYOUT), true);

  vv_text(c, "PRESENT SUB-PHASES", VV_STYLE(.fg = t->accent_hi, .font_size = 11));
  for (vv_PerfPhaseID id = VV_PERF_PRESENT_STYLE; id <= VV_PERF_PRESENT_EXITING; id++)
    vvph_bar(c, vvph_short_name(id), vvph_short_name(id), hud->ms[id], 0,
             vvph_phase_color(VV_PERF_PRESENT), true);
  (void)vvph_is_child;
}

static void vvph_timeline(vv_Ctx *c, vv_PerfHud *hud) {
  const vv_Theme *t = vv_theme();
  int n = hud->filled < 8 ? hud->filled : VV_PERF_HUD_HISTORY;
  // plot the tail of the ring so a cold HUD doesn't draw a wall of zeros
  int off = VV_PERF_HUD_HISTORY - n;

  // flat budget reference across the visible window
  for (int i = 0; i < VV_PERF_HUD_HISTORY; i++) hud->hist_budget[i] = VVPH_BUDGET_MS;

  vv_text(c, "FRAME TIME (ms) \xc2\xb7 last 128 frames",
          VV_STYLE(.fg = t->accent_hi, .font_size = 11));
  {
    vv_PlotSeries s[2] = {
        {.ys = hud->hist_budget + off, .count = n,
         .color = vv_rgba(0.95f, 0.42f, 0.42f, 0.5f), .kind = VV_PLOT_LINE,
         .width = 1.0f, .name = "16.6ms"},
        {.ys = hud->hist_frame + off, .count = n, .color = vv_rgb(0.38f, 0.72f, 0.95f),
         .kind = VV_PLOT_LINE, .width = 2.0f, .name = "frame"},
    };
    vv_plot(c, "pf", s, 2,
            (vv_PlotOpts){.y_min = 0, .auto_y = true, .auto_x = true,
                          .grid = true, .height = 120});
  }

  vv_text(c, "PHASE BREAKDOWN (ms)", VV_STYLE(.fg = t->accent_hi, .font_size = 11));
  {
    vv_PlotSeries s[3] = {
        {.ys = hud->hist_input + off, .count = n,
         .color = vvph_phase_color(VV_PERF_INPUT), .kind = VV_PLOT_LINE,
         .width = 1.5f, .name = "input"},
        {.ys = hud->hist_layout + off, .count = n,
         .color = vvph_phase_color(VV_PERF_LAYOUT), .kind = VV_PLOT_LINE,
         .width = 1.5f, .name = "layout"},
        {.ys = hud->hist_present + off, .count = n,
         .color = vvph_phase_color(VV_PERF_PRESENT), .kind = VV_PLOT_LINE,
         .width = 1.5f, .name = "present"},
    };
    vv_plot(c, "pp", s, 3,
            (vv_PlotOpts){.y_min = 0, .auto_y = true, .auto_x = true,
                          .grid = true, .height = 110});
  }
}

static void vvph_view(vv_Ctx *c, void *st) {
  vv_PerfHud *hud = st;
  const vv_Theme *t = vv_theme();
  // FRAME_TOTAL is stamped once per vv_run_frame; non-zero means the core is
  // instrumented (-DVV_PERF) and has run at least one frame.
  bool have_data = hud->target->perf.perf.samples[VV_PERF_FRAME_TOTAL].frames > 0;

  if (hud->windowed)
    // fills its own native window, edge to edge
    vv_box_keyed(c, "card", 4,
                 VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                           .scroll_y = true, .clip = true,
                           .padding = vv_all(14), .gap = 10),
                 VV_STYLE(.bg = vv_rgb(0.06f, 0.07f, 0.09f)));
  else
    // a floating card docked to the app's top-left
    vv_box_keyed(c, "card", 4,
                 VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(hud->panel_w),
                           .has_absolute = true,
                           .absolute = vv_rect(hud->x, hud->y, hud->panel_w, 0),
                           .padding = vv_all(14), .gap = 10),
                 VV_STYLE(.bg = vv_rgba(0.06f, 0.07f, 0.09f, 0.96f),
                          .radius = vv_r(12), .border_width = vv_all(1),
                          .border_color = t->border));
  {
    // title row
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                        .main = VV_ALIGN_SPACE_BETWEEN),
           VV_STYLE(.bg = {0})) {
      vv_text(c, "\xe2\x9a\xa1 Performance", VV_STYLE(.fg = t->text, .font_size = 16));
      vv_text(c, "F11", VV_STYLE(.fg = t->text_muted, .font_size = 11));
    }

    if (!have_data) {
      vv_text(c, "No timing data.", VV_STYLE(.fg = t->text, .font_size = 13));
      vv_text(c, "Rebuild core + app with -DVV_PERF",
              VV_STYLE(.fg = t->text_muted, .font_size = 12));
      vv_text(c, "e.g.  make VV_PERF=1 perf_demo",
              VV_STYLE(.fg = t->accent_hi, .font_size = 12));
      vv_text(c, "and call VV_PERF_INIT(&ctx).",
              VV_STYLE(.fg = t->text_muted, .font_size = 12));
      vv_end_box(c);
      return;
    }

    // tabs
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 6), VV_STYLE(.bg = {0})) {
      if (vvph_tab(c, "t0", "Overview", hud->tab == 0)) hud->tab = 0;
      if (vvph_tab(c, "t1", "Phases", hud->tab == 1))   hud->tab = 1;
      if (vvph_tab(c, "t2", "Timeline", hud->tab == 2)) hud->tab = 2;
    }

    // content
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 8),
           VV_STYLE(.bg = {0})) {
      if (hud->tab == 0) vvph_overview(c, hud);
      else if (hud->tab == 1) vvph_phases(c, hud);
      else vvph_timeline(c, hud);
    }
  }
  vv_end_box(c);
}

void vv_perf_hud_init(vv_PerfHud *hud, vv_Ctx *target, vv_MeasureFn measure,
                      void *ud) {
  memset(hud, 0, sizeof *hud);
  hud->target = target;
  hud->open = true;
  hud->tab = 0;
  hud->panel_w = 340.0f;
  hud->x = 16.0f;
  hud->y = 16.0f;
  vv_init(&hud->ctx);
  vv_set_measure_fn(&hud->ctx, measure, ud);
}

void vv_perf_hud_toggle(vv_PerfHud *hud) { hud->open = !hud->open; }

vv_Input vv_perf_hud_split(vv_PerfHud *hud, vv_Input raw, float w, float h) {
  (void)w; (void)h;
  if (!hud->open) { hud->over_panel = false; return raw; }

  // Route the pointer to whichever surface it is over. The card grows downward
  // from (x,y); we don't know its height until it lays out, so use a generous
  // hit region: the panel column strip below y. Good enough for a HUD.
  bool over = raw.mouse.x >= hud->x && raw.mouse.x <= hud->x + hud->panel_w &&
              raw.mouse.y >= hud->y;
  hud->over_panel = over;

  vv_Input app_in = raw, ui_in = raw;
  if (over) { app_in.mouse_down = false; app_in.mouse = vv_v2(-1, -1); }
  else      { ui_in.mouse_down = false;  ui_in.mouse = vv_v2(-1, -1); }
  hud->ui_in = ui_in;

  // Keep the app rebuilding so the HUD always has a fresh full-pipeline frame to
  // read (and an idle app doesn't blank under the cleared framebuffer).
  vv_invalidate(hud->target);
  return app_in;
}

vv_CommandBuffer *vv_perf_hud_render(vv_PerfHud *hud, float dt, float w, float h,
                                     float dpi) {
  if (!hud->open) return NULL;

  vvph_sample(hud, dt);

  vv_set_window(&hud->ctx, w, h, dpi);
  vv_invalidate(&hud->ctx); // rebuild every frame to animate the graphs
  return vv_run_frame(&hud->ctx, dt, &hud->ui_in, NULL, vvph_view, hud);
}

vv_CommandBuffer *vv_perf_hud_window(vv_PerfHud *hud, float dt, float wall_dt,
                                     float w, float h, float dpi, vv_Input win_in) {
  hud->windowed = true;
  vvph_sample(hud, wall_dt);           // measure the app's cadence, not this window's
  vv_set_window(&hud->ctx, w, h, dpi);
  vv_invalidate(&hud->ctx);
  return vv_run_frame(&hud->ctx, dt, &win_in, NULL, vvph_view, hud);
}

#endif // VV_PERF_HUD_IMPL
