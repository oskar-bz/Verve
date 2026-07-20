// theme_editor.c — a live editor for a vv_Theme, and a tour of the desktop
// features it leans on. Because a theme is just values, swapping it animates for
// free (§7.1): every edit springs the whole UI — editor and preview alike — to
// the new palette.
//
//   • Theme library   — the Preset picker applies any built-in palette (Verve
//     dark/light, classic Win32, WinUI/Fluent, GNOME Adwaita, Nord, Solarized)
//     from vv_theme.h; tweak it from there.
//   • Multiline / popovers / tooltips — click a swatch to open a color popover
//     (R/G/B sliders, dismiss on outside-click); swatches have hover tooltips.
//   • Native dialogs  — File > Save/Open theme write/read a small text format
//     via the library's vv_theme_save / vv_theme_load — any app can do the same.
//   • Multi-window    — Window > Detach preview opens a real second window that
//     renders the same preview with the live theme.
//
// Editor + preview both read vv_theme(), so the editor restyles itself as you
// edit — which is exactly what a theme editor should show.
#include "verve/verve.h"
#include "verve/vv_theme.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  MSG_EDIT_FIELD = 1, MSG_POP_CLOSE, MSG_R, MSG_G, MSG_B,
  MSG_RESET, MSG_OPEN, MSG_SAVE, MSG_DETACH,
  MSG_PRESET,
  MSG_PV_TEXT, MSG_PV_CHECK, MSG_PV_TOGGLE, MSG_PV_SLIDER,
  MSG_ANIM_RESP, MSG_ANIM_DAMP, MSG_ANIM_SPEED,
  // One message per scalar metric, in vv_theme_metrics order.
  MSG_METRIC0,
};
// Map metric index -> message (contiguous from MSG_METRIC0).
#define METRIC_MSG(i) (vv_Msg)(MSG_METRIC0 + (i))

// The editable colour fields come from the library's introspection table
// (vv_theme_fields) — name + offset into vv_Theme. NFIELDS sizes the per-row
// arrays; main() asserts it matches vv_theme_field_count at startup.
enum { NFIELDS = 12 };

// Display names for the preset combobox, mirrored from vv_theme_presets in
// main() (the library owns the list; we just point at its strings).
static const char *g_preset_names[16];

typedef struct {
  vv_Theme theme;
  float    anim_response, anim_damping, anim_speed; // global motion feel (§6.1)
  int      preset;      // index into vv_theme_presets shown in the picker
  unsigned theme_rev;   // bumped on every theme edit; drives child-preview rebuilds
  int      editing;     // field index whose popover is open, -1 = none
  bool     pop_open;
  uint32_t row_id[NFIELDS]; // captured in view, anchors popover + tooltips

  char  ch_buf[3][8];   // 0-255 text for the R/G/B channel inputs (popover)

  // interactive preview state, so the preview reacts (hover/checked/etc.)
  bool  pv_check, pv_toggle;
  float pv_slider;
  char  pv_text[64];

  char    status[256];
  vv_Ctx *ctx;
  vv_App *app;
  int     want_detach;
} App;

static vv_Color *field_ptr(App *a, int i) {
  return (vv_Color *)((char *)&a->theme + vv_theme_fields[i].off);
}

// ---- theme (de)serialization: the library owns the format now --------------
// These are just dialog callbacks that call vv_theme_save / vv_theme_load and
// report the result; any Verve app can load and apply a theme the same way.
static void save_theme(void *ud, const char *path) {
  App *a = ud;
  if (!path) snprintf(a->status, sizeof a->status, "Save cancelled");
  else if (vv_theme_save(&a->theme, path)) snprintf(a->status, sizeof a->status, "Saved %s", path);
  else snprintf(a->status, sizeof a->status, "Cannot write %s", path);
  if (a->ctx) vv_invalidate(a->ctx);
}
static void load_theme(void *ud, const char *path) {
  App *a = ud;
  if (!path) { snprintf(a->status, sizeof a->status, "Open cancelled"); }
  else if (vv_theme_load(&a->theme, path)) {
    snprintf(a->status, sizeof a->status, "Loaded %s", path);
    a->theme_rev++;
  } else {
    snprintf(a->status, sizeof a->status, "Cannot read %s", path);
  }
  if (a->ctx) vv_invalidate(a->ctx);
}

static void update(void *st, vv_Event ev) {
  App *a = st;
  vv_Color *c = (a->editing >= 0 && a->editing < NFIELDS) ? field_ptr(a, a->editing) : NULL;

  // A scalar metric slider (radius / border / padding / gap / font)?
  int mi = (int)ev.msg - MSG_METRIC0;
  if (mi >= 0 && mi < vv_theme_metric_count) {
    vv_theme_metric_set(&a->theme, mi, (float)ev.data.as_float);
    a->theme_rev++;
    return;
  }

  switch (ev.msg) {
  case MSG_EDIT_FIELD: a->editing = ev.data.as_int; a->pop_open = true; break;
  case MSG_POP_CLOSE:  a->pop_open = false; break;
  case MSG_R: if (c) c->r = (float)ev.data.as_float; break;
  case MSG_G: if (c) c->g = (float)ev.data.as_float; break;
  case MSG_B: if (c) c->b = (float)ev.data.as_float; break;
  case MSG_RESET:  snprintf(a->status, sizeof a->status, "Reset"); a->theme = vv_theme_dark(); break;
  case MSG_OPEN: vv_app_open_file(a->app, "Verve theme", "vvtheme;txt", load_theme, a); break;
  case MSG_SAVE: vv_app_save_file(a->app, "Verve theme", "vvtheme;txt", "theme.vvtheme", save_theme, a); break;
  case MSG_PRESET: {  // apply a built-in palette from the library
    int p = ev.data.as_int;
    if (p >= 0 && p < vv_theme_preset_count) {
      a->preset = p;
      a->theme = vv_theme_presets[p].make();
      snprintf(a->status, sizeof a->status, "Applied %s", vv_theme_presets[p].name);
    }
  } break;
  case MSG_DETACH: a->want_detach++; break;
  case MSG_ANIM_RESP:  a->anim_response = (float)ev.data.as_float; break;
  case MSG_ANIM_DAMP:  a->anim_damping  = (float)ev.data.as_float; break;
  case MSG_ANIM_SPEED: a->anim_speed    = (float)ev.data.as_float; break;
  case MSG_PV_TEXT:   break; // text_field edits pv_text in place
  case MSG_PV_CHECK:  a->pv_check = ev.data.as_int; break;
  case MSG_PV_TOGGLE: a->pv_toggle = ev.data.as_int; break;
  case MSG_PV_SLIDER: a->pv_slider = (float)ev.data.as_float; break;
  }
  // Any theme mutation bumps the revision so detached previews rebuild.
  switch (ev.msg) {
  case MSG_R: case MSG_G: case MSG_B:
  case MSG_RESET: case MSG_PRESET: a->theme_rev++; break;
  default: break;
  }
}

// ---- the preview: representative widgets, styled entirely by the live theme --
static void preview(vv_Ctx *c, App *a) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(20), .gap = 14, .scroll_y = true, .clip = true),
         VV_STYLE(.bg = t->surface)) {
    vv_text(c, "Preview", VV_STYLE(.fg = t->text, .font_size = t->font_size + 6));
    vv_text(c, "Every widget below reads the live theme.",
            VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 1));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
      vv_button(c, "pvb1", "Primary", MSG_PV_CHECK, vv_pi(!a->pv_check));
      vv_button(c, "pvb2", "Secondary", MSG_PV_TOGGLE, vv_pi(!a->pv_toggle));
    }
    vv_checkbox(c, "pvc", "Enable option", a->pv_check, MSG_PV_CHECK);
    vv_toggle(c, "pvt", a->pv_toggle, MSG_PV_TOGGLE);
    vv_slider(c, "pvs", a->pv_slider, 0, 1, MSG_PV_SLIDER);
    vv_text_field(c, "pvf", a->pv_text, (int)sizeof a->pv_text, "Type here...", MSG_PV_TEXT);

    vv_list_item(c, "pl1", "Selected list item", true, MSG_PV_CHECK, vv_pi(!a->pv_check));
    vv_list_item(c, "pl2", "Unselected list item", false, MSG_PV_CHECK, vv_pi(!a->pv_check));

    // A card exercising radius / border / shadow / danger.
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .padding = vv_all(16), .gap = 8),
           VV_STYLE(.bg = t->surface_hi, .radius = vv_r(t->radius),
                    .border_width = vv_all(t->border_width), .border_color = t->border,
                    .shadow = {.color = vv_rgba(0, 0, 0, 0.3f), .offset = vv_v2(0, 4), .blur = 14})) {
      vv_text(c, "Card", VV_STYLE(.fg = t->text, .font_size = t->font_size + 2));
      vv_text(c, "Rounded to the theme radius, with a border and shadow.",
              VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 1));
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .padding = vv_hv(12, 8), .cross = VV_ALIGN_CENTER),
             VV_STYLE(.bg = t->danger, .radius = vv_r(t->radius))) {
        vv_text(c, "Danger", VV_STYLE(.fg = t->on_accent, .font_size = t->font_size - 1));
      }
    }
  }
}

static void color_row(vv_Ctx *c, App *a, int i) {
  const vv_Theme *t = vv_theme();
  vv_Color *col = field_ptr(a, i);
  vv_Style hover = {.bg = t->surface_hi};
  uint32_t id = vv_box_keyed(c, vv_theme_fields[i].name, strlen(vv_theme_fields[i].name),
                             (vv_LayoutDecl){.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(34),
                                             .cross = VV_ALIGN_CENTER, .gap = 10,
                                             .padding = vv_hv(8, 0), .focusable = true},
                             (vv_Style){.radius = vv_r(6), .hover = &hover});
  a->row_id[i] = id;
  // swatch
  vv_box_keyed(c, "sw", 2,
               (vv_LayoutDecl){.w = vv_fixed(26), .h = vv_fixed(20)},
               (vv_Style){.bg = *col, .radius = vv_r(4),
                          .border_width = vv_all(1), .border_color = t->border});
  vv_end_box(c);
  vv_text(c, vv_theme_fields[i].name, VV_STYLE(.fg = t->text, .font_size = t->font_size));
  VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
  vv_text(c, vv_fmt(c, "%.2f %.2f %.2f", (double)col->r, (double)col->g, (double)col->b),
          VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 3));
  vv_end_box(c);
  if (vv_clicked(c, id)) vv_emit(c, MSG_EDIT_FIELD, vv_pi(i));
}

// One R/G/B channel: a slider and a numeric (0-255) text field that edit the
// same value — a hybrid input. The slider streams float 0..1 via `msg`; the
// field parses 0-255 while focused and emits the same `msg`, so either control
// drives the colour. When the field isn't focused it mirrors the live value.
static void channel(vv_Ctx *c, App *a, int ci, const char *label, vv_Color lc,
                    float *val, const char *skey, const char *tkey, vv_Msg msg) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER, .gap = 8),
         VV_STYLE(.bg = {0})) {
    vv_text(c, label, VV_STYLE(.fg = lc, .font_size = t->font_size));
    vv_slider(c, skey, *val, 0, 1, msg);
    // Numeric field (0-255). Mirror the live value while unfocused; parse the
    // typed text and emit `msg` while focused. MSG_PV_TEXT is a no-op handler —
    // the value flows through `msg` below, not the field's own change message.
    uint32_t f = 0;
    VV_BOX(c, VV_LAYOUT(.w = vv_fixed(52)), VV_STYLE(.bg = {0})) {
      f = vv_text_field(c, tkey, a->ch_buf[ci], (int)sizeof a->ch_buf[ci], NULL,
                        MSG_PV_TEXT);
    }
    if (vv_focused(c, f)) {
      int iv = atoi(a->ch_buf[ci]);
      iv = iv < 0 ? 0 : (iv > 255 ? 255 : iv);
      vv_emit(c, msg, vv_pf((float)iv / 255.0f));
    } else {
      snprintf(a->ch_buf[ci], sizeof a->ch_buf[ci], "%d",
               (int)lroundf(*val * 255.0f));
    }
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  vv_set_theme(&a->theme);          // the whole UI springs to the edited palette
  vv_set_default_spring(c, (vv_SpringParams){a->anim_response, a->anim_damping});
  vv_set_animation_scale(c, a->anim_speed);
  const vv_Theme *t = vv_theme();
  uint32_t m_file = 0, m_win = 0;

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.07f, 0.08f, 0.10f))) {

    vv_menubar_begin(c);
    m_file = vv_menu_title(c, "file", "File");
    m_win  = vv_menu_title(c, "window", "Window");
    vv_menubar_end(c);

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)),
           VV_STYLE(.bg = {0})) {

      // Left: the editor.
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(340), .h = vv_grow(1),
                          .padding = vv_all(14), .gap = 6, .scroll_y = true, .clip = true),
             VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f),
                      .border_width = (vv_Edges){0, 0, 1, 0}, .border_color = t->border)) {
        // Preset picker: apply any theme from the library, then tweak it.
        vv_text(c, "Preset", VV_STYLE(.fg = t->text, .font_size = t->font_size + 4));
        vv_combobox(c, "preset", g_preset_names, vv_theme_preset_count, a->preset, MSG_PRESET);

        vv_text(c, "Colours", VV_STYLE(.fg = t->text, .font_size = t->font_size + 4));
        for (int i = 0; i < NFIELDS; i++) color_row(c, a, i);

        // Metrics: every scalar (radius/border/padding/gap/font) from the
        // library's introspection table gets a slider automatically.
        vv_text(c, "Metrics", VV_STYLE(.fg = t->text, .font_size = t->font_size + 4));
        for (int i = 0; i < vv_theme_metric_count; i++) {
          const vv_ThemeMetric *m = &vv_theme_metrics[i];
          VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER, .gap = 10),
                 VV_STYLE(.bg = {0})) {
            VV_BOX(c, VV_LAYOUT(.w = vv_fixed(96)), VV_STYLE(.bg = {0})) {
              vv_text(c, vv_fmt(c, "%s %.1f", m->name, (double)vv_theme_metric_get(&a->theme, i)),
                      VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2));
            }
            vv_slider(c, vv_fmt(c, "m%d", i), vv_theme_metric_get(&a->theme, i),
                      m->lo, m->hi, METRIC_MSG(i));
          }
        }

        // Animation: the global spring feel (§6.1) + motion-speed kill switch.
        vv_text(c, "Animation", VV_STYLE(.fg = t->text, .font_size = t->font_size + 4));
        struct { const char *name; float val, lo, hi; vv_Msg msg; } anim[] = {
          {"response", a->anim_response, 0.05f, 0.6f, MSG_ANIM_RESP},
          {"damping",  a->anim_damping,  0.4f,  1.2f, MSG_ANIM_DAMP},
          {"speed",    a->anim_speed,    0.0f,  2.0f, MSG_ANIM_SPEED},
        };
        for (int i = 0; i < 3; i++) {
          VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER, .gap = 10),
                 VV_STYLE(.bg = {0})) {
            VV_BOX(c, VV_LAYOUT(.w = vv_fixed(96)), VV_STYLE(.bg = {0})) {
              vv_text(c, vv_fmt(c, "%s %.2f", anim[i].name, (double)anim[i].val),
                      VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2));
            }
            vv_slider(c, vv_fmt(c, "a%d", i), anim[i].val, anim[i].lo, anim[i].hi, anim[i].msg);
          }
        }

        vv_text(c, a->status, VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 3));
      }

      // Right: the live preview.
      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1)), VV_STYLE(.bg = {0})) {
        preview(c, a);
      }
    }
  }

  // ===== overlay layer ======================================================
  if (vv_menu_is_open(c, m_file)) {
    vv_Rect r = vv_node(c, m_file)->actual_rect;
    vv_menu_begin(c, "filemenu", vv_v2(r.x, r.y + r.h));
    if (vv_menu_item(c, "mopen", "Open theme...", "Ctrl+O")) vv_emit(c, MSG_OPEN, VV_NO_PAYLOAD);
    if (vv_menu_item(c, "msave", "Save theme...", "Ctrl+S")) vv_emit(c, MSG_SAVE, VV_NO_PAYLOAD);
    vv_menu_separator(c);
    if (vv_menu_item(c, "mreset", "Reset to dark", NULL)) vv_emit(c, MSG_RESET, VV_NO_PAYLOAD);
    vv_menu_end(c);
  }
  if (vv_menu_is_open(c, m_win)) {
    vv_Rect r = vv_node(c, m_win)->actual_rect;
    vv_menu_begin(c, "winmenu", vv_v2(r.x, r.y + r.h));
    if (vv_menu_item(c, "wdet", "Detach preview window", NULL)) vv_emit(c, MSG_DETACH, VV_NO_PAYLOAD);
    vv_menu_end(c);
  }

  // Colour popover for the field being edited.
  if (a->pop_open && a->editing >= 0) {
    vv_Color *col = field_ptr(a, a->editing);
    vv_Rect r = vv_node(c, a->row_id[a->editing])->actual_rect;
    vv_popover_begin(c, "cpop", vv_v2(r.x + r.w - 20, r.y + r.h), 240, MSG_POP_CLOSE);
    vv_text(c, vv_fmt(c, "Edit %s", vv_theme_fields[a->editing].name),
            VV_STYLE(.fg = t->text, .font_size = t->font_size + 1));
    // a big swatch of the current value
    vv_box_keyed(c, "big", 3, (vv_LayoutDecl){.w = vv_grow(1), .h = vv_fixed(30)},
                 (vv_Style){.bg = *col, .radius = vv_r(6),
                            .border_width = vv_all(1), .border_color = t->border});
    vv_end_box(c);
    channel(c, a, 0, "R", t->danger, &col->r, "cr", "crt", MSG_R);
    channel(c, a, 1, "G", vv_rgb(0.4f, 0.85f, 0.4f), &col->g, "cg", "cgt", MSG_G);
    channel(c, a, 2, "B", t->accent, &col->b, "cb", "cbt", MSG_B);
    vv_popover_end(c);
  }

  // Tooltips on every swatch row.
  for (int i = 0; i < NFIELDS; i++)
    vv_tooltip(c, a->row_id[i], vv_fmt(c, "Click to edit %s", vv_theme_fields[i].name));
}

// Detached preview window: its own context, same live theme + state.
static void view_detached(vv_Ctx *c, void *st) {
  App *a = st;
  vv_set_theme(&a->theme);
  preview(c, a);
}

typedef struct { vv_App *app; vv_Ctx ctx; bool used; unsigned seen_rev; } Child;

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Theme Editor", 1000, 660);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true); // sleep when settled; springs keep us awake
  vv_app_bind_clipboard(app, &ctx);

  // The editor's colour list must line up with the library's introspection.
  assert(NFIELDS == vv_theme_field_count);
  for (int i = 0; i < vv_theme_preset_count && i < 16; i++)
    g_preset_names[i] = vv_theme_presets[i].name;

  static App state;
  state.theme = vv_theme_dark();
  state.editing = -1;
  state.anim_response = 0.25f; state.anim_damping = 1.0f; state.anim_speed = 1.0f;
  state.pv_slider = 0.6f;
  snprintf(state.pv_text, sizeof state.pv_text, "Hello");
  snprintf(state.status, sizeof state.status, "%d colours - edit and watch it spring", NFIELDS);
  state.app = app; state.ctx = &ctx;

  Child children[2] = {0};

  while (vv_app_pump_all() > 0) {
    bool drew = false; // any window that renders keeps us off the event-wait
    static uint64_t prev; if (!prev) prev = SDL_GetPerformanceCounter();
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;
    if (vv_app_should_close(app)) break;

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, vv_app_input(app), update, view, &state);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    if (cmds) {
      drew = true;
      vv_app_frame_begin(app, vv_rgb(0.07f, 0.08f, 0.10f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }

    while (state.want_detach > 0) {
      state.want_detach--;
      for (int i = 0; i < 2; i++) {
        if (children[i].used) continue;
        children[i].app = vv_app_open_child(app, "Verve \xc2\xb7 Live Preview", 420, 560);
        if (!children[i].app) break;
        vv_init(&children[i].ctx);
        vv_set_measure_fn(&children[i].ctx, vv_app_measure, children[i].app);
        vv_set_idle_mode(&children[i].ctx, true);
        children[i].seen_rev = state.theme_rev;
        children[i].used = true;
        break;
      }
    }
    for (int i = 0; i < 2; i++) {
      if (!children[i].used) continue;
      if (vv_app_should_close(children[i].app)) {
        vv_shutdown(&children[i].ctx); vv_app_destroy(children[i].app);
        children[i].used = false; continue;
      }
      // A theme edit in the main window mutates shared state the child reads,
      // but the child gets no input of its own — so idle mode would keep it
      // parked on a stale build. Force a rebuild when the palette moved.
      if (children[i].seen_rev != state.theme_rev) {
        vv_invalidate(&children[i].ctx);
        children[i].seen_rev = state.theme_rev;
      }
      int cw, ch; float cdpi; vv_app_size(children[i].app, &cw, &ch, &cdpi);
      vv_set_window(&children[i].ctx, (float)cw, (float)ch, cdpi);
      vv_CommandBuffer *cc = vv_run_frame(&children[i].ctx, dt, vv_app_input(children[i].app),
                                          update, view_detached, &state);
      if (cc) {
        drew = true;
        vv_app_frame_begin(children[i].app, vv_rgb(0.07f, 0.08f, 0.10f));
        vv_render(vv_app_backend(children[i].app), cc, cw, ch, cdpi);
        vv_app_frame_end(children[i].app);
      }
    }

    // Everything settled and idle: block for the next event instead of
    // busy-spinning. A window with springs still in flight returns a command
    // buffer (drew==true), so animations — including resize/maximize FLIP and
    // the enter transition on first open — keep ticking without needing stray
    // mouse motion to wake the loop.
    if (!drew) vv_app_wait_event(app, 16);
  }

  for (int i = 0; i < 2; i++)
    if (children[i].used) { vv_shutdown(&children[i].ctx); vv_app_destroy(children[i].app); }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
