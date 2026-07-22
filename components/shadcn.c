// shadcn.c — a slice of shadcn/ui rebuilt on Verve, plus a gallery.
//
// The point of this file is the *pattern* for mimicking a design system (see the
// README answer): the look lives entirely in a vv_Theme of design tokens, and
// each component is a small function that maps a `variant`/`size` enum onto
// those tokens. Swap the theme (the header switch) and every component reskins —
// and animates there for free, because a theme is just values (§7.1).
//
//   make shadcn   ->   ./build/shadcn
#include "verve/verve.h"      // vv_theme_complete is declared in vv_widgets.h
#include "vv_sdl_gl.h"        // vv_app_run
#include <stdio.h>
#include <string.h>

// Font ids, matching the order in demo_fonts() below (loaded 0,1,2). Rich text
// gets real weight/slant by pointing a span at the bold/italic face.
enum { FONT_REGULAR = 0, FONT_BOLD = 1, FONT_ITALIC = 2 };

// A regular/bold/italic triplet from whichever family is installed, so
// vv_rich_text can show true bold + italic (not just colour/size emphasis).
static const char *const *demo_fonts(void) {
  static const char *noto[] = {
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/noto/NotoSans-Bold.ttf",
      "/usr/share/fonts/noto/NotoSans-Italic.ttf", NULL};
  static const char *lib[] = {
      "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Italic.ttf", NULL};
  FILE *f = fopen(noto[0], "rb");
  if (f) { fclose(f); return (const char *const *)noto; }
  f = fopen(lib[0], "rb");
  if (f) { fclose(f); return (const char *const *)lib; }
  return NULL; // fall back to vv_app_run's default (regular only)
}

// ---------------------------------------------------------------------------
// Themes: shadcn's default "zinc" palette, expressed in Verve design tokens.
// We only spell out the semantic roles; vv_theme_complete fills the radius and
// spacing scales. Editing these is how you'd retheme the whole component set.
// ---------------------------------------------------------------------------
static vv_Color hex(unsigned rgb) {
  return vv_rgb((float)((rgb >> 16) & 0xff) / 255.0f,
                (float)((rgb >> 8) & 0xff) / 255.0f,
                (float)(rgb & 0xff) / 255.0f);
}
static vv_Color mix(vv_Color a, vv_Color b, float t) {
  return vv_rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

static vv_Theme shadcn_light(void) {
  return vv_theme_complete((vv_Theme){
      .surface_app = hex(0xffffff), .surface_panel = hex(0xfafafa),
      .surface_card = hex(0xffffff), .surface_overlay = hex(0xffffff),
      .control_bg_rest = hex(0xf4f4f5), .control_bg_hover = hex(0xe9e9ec),
      .control_bg_active = hex(0xe4e4e7), .control_bg_disabled = hex(0xf4f4f5),
      .text_primary = hex(0x09090b), .text_secondary = hex(0x3f3f46),
      .text_muted = hex(0x71717a), .text_inverse = hex(0xfafafa), .text_on_brand = hex(0xfafafa),
      .border_subtle = hex(0xf1f1f3), .border_default = hex(0xe4e4e7),
      .border_strong = hex(0xd4d4d8), .border_focus = hex(0xa1a1aa),
      .brand_primary = hex(0x18181b), .brand_hover = hex(0x27272a),
      .brand_active = hex(0x000000), .brand_subtle = hex(0xf4f4f5),
      .status_error = hex(0xef4444), .status_error_subtle = hex(0xfef2f2),
      .status_warning = hex(0xf59e0b), .status_success = hex(0x16a34a), .status_info = hex(0x3b82f6),
      .radius = 8.0f, .border_width = 1.0f, .pad_x = 16.0f, .pad_y = 9.0f, .gap = 8.0f, .font_size = 14.0f,
  });
}

static vv_Theme shadcn_dark(void) {
  return vv_theme_complete((vv_Theme){
      .surface_app = hex(0x09090b), .surface_panel = hex(0x131316),
      .surface_card = hex(0x18181b), .surface_overlay = hex(0x18181b),
      .control_bg_rest = hex(0x27272a), .control_bg_hover = hex(0x323236),
      .control_bg_active = hex(0x3f3f46), .control_bg_disabled = hex(0x27272a),
      .text_primary = hex(0xfafafa), .text_secondary = hex(0xd4d4d8),
      .text_muted = hex(0xa1a1aa), .text_inverse = hex(0x18181b), .text_on_brand = hex(0x18181b),
      .border_subtle = hex(0x1d1d20), .border_default = hex(0x27272a),
      .border_strong = hex(0x3f3f46), .border_focus = hex(0xd4d4d8),
      .brand_primary = hex(0xfafafa), .brand_hover = hex(0xe4e4e7),
      .brand_active = hex(0xffffff), .brand_subtle = hex(0x27272a),
      .status_error = hex(0xef4444), .status_error_subtle = hex(0x2a1416),
      .status_warning = hex(0xf59e0b), .status_success = hex(0x22c55e), .status_info = hex(0x60a5fa),
      .radius = 8.0f, .border_width = 1.0f, .pad_x = 16.0f, .pad_y = 9.0f, .gap = 8.0f, .font_size = 14.0f,
  });
}

// ===========================================================================
// Components. Each is a plain function: variant/size -> tokens -> boxes.
// ===========================================================================

typedef enum {
  BTN_DEFAULT, BTN_SECONDARY, BTN_DESTRUCTIVE, BTN_OUTLINE, BTN_GHOST, BTN_LINK,
} ShadcnBtnVariant;
typedef enum { SZ_SM, SZ_DEFAULT, SZ_LG, SZ_ICON } ShadcnSize;

uint32_t shadcn_button(vv_Ctx *c, const char *key, const char *label,
                       ShadcnBtnVariant variant, ShadcnSize size, bool disabled,
                       vv_Msg click, vv_Payload arg) {
  const vv_Theme *t = vv_theme();

  // variant -> fill / text / border + the hover fill.
  vv_Color bg = {0}, fg = t->text_primary, bd = {0}, hov = t->control_bg_hover;
  float bw = 0.0f;
  switch (variant) {
  case BTN_DEFAULT:     bg = t->brand_primary;   fg = t->text_on_brand; hov = t->brand_hover; break;
  case BTN_SECONDARY:   bg = t->control_bg_rest;  fg = t->text_primary;  hov = t->control_bg_hover; break;
  case BTN_DESTRUCTIVE: bg = t->status_error;     fg = t->text_on_brand; hov = mix(t->status_error, t->text_primary, 0.12f); break;
  case BTN_OUTLINE:     bg = t->surface_app;      fg = t->text_primary;  hov = t->control_bg_hover; bd = t->border_default; bw = t->border_width; break;
  // Ghost rests transparent but on the *hover colour* (alpha 0), so the bg
  // spring only ramps alpha in — interpolating from plain {0} would pass through
  // a semi-transparent dark grey and read as a flicker.
  case BTN_GHOST:       hov = t->control_bg_hover; bg = vv_rgba(hov.r, hov.g, hov.b, 0.0f); fg = t->text_primary; break;
  case BTN_LINK:        bg = (vv_Color){0};       fg = t->brand_primary; hov = (vv_Color){0}; break;
  }

  // size -> padding + font size. ICON is a square.
  float px = 16, py = 9, fs = t->font_size;
  switch (size) {
  case SZ_SM:   px = 12; py = 6;  fs = t->font_size - 1; break;
  case SZ_LG:   px = 22; py = 11; fs = t->font_size + 1; break;
  case SZ_ICON: px = 9;  py = 9;  break;
  default: break;
  }

  vv_Style hover  = {.bg = hov};
  vv_Style active = {.transform = vv_scale(0.97f)};
  // Outset focus ring (offset 2, 2px) — shadcn's signature ring. It's an outline
  // outside the box, so it never nudges the label; the ring colour springs, so
  // it fades in/out on its own.
  vv_Style focus  = {.ring_color = t->border_focus, .ring_width = 2,
                     .ring_offset = 2, .set = VV_STYLE_RING};
  uint32_t id = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = vv_fit(), .h = vv_fit(), .padding = vv_hv(px, py),
                .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER,
                .focusable = !disabled,
                .cursor = disabled ? VV_CURSOR_DEFAULT : VV_CURSOR_POINTER),
      VV_STYLE(.bg = bg, .radius = vv_r(t->radius_md), .border_color = bd,
               .border_width = vv_all(bw), .opacity = disabled ? 0.5f : 1.0f,
               .hover = disabled ? NULL : &hover,
               .active = disabled ? NULL : &active, .focus = &focus));
  vv_text(c, label, VV_STYLE(.fg = fg, .font_size = fs, .font = t->font));
  vv_end_box(c);
  if (!disabled && (vv_clicked(c, id) || vv_activated(c, id)))
    vv_emit(c, click, arg);
  return id;
}

typedef enum {
  BADGE_DEFAULT, BADGE_SECONDARY, BADGE_DESTRUCTIVE, BADGE_OUTLINE,
} ShadcnBadgeVariant;

void shadcn_badge(vv_Ctx *c, const char *key, const char *label,
                  ShadcnBadgeVariant variant) {
  const vv_Theme *t = vv_theme();
  vv_Color bg, fg, bd = {0};
  float bw = 0.0f;
  switch (variant) {
  case BADGE_SECONDARY:   bg = t->control_bg_rest; fg = t->text_primary; break;
  case BADGE_DESTRUCTIVE: bg = t->status_error;    fg = t->text_on_brand; break;
  case BADGE_OUTLINE:     bg = (vv_Color){0};      fg = t->text_primary; bd = t->border_default; bw = t->border_width; break;
  default:                bg = t->brand_primary;   fg = t->text_on_brand; break;
  }
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.padding = vv_hv(10, 3), .main = VV_ALIGN_CENTER,
                         .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = bg, .radius = vv_r(t->radius_full),
                        .border_color = bd, .border_width = vv_all(bw)));
  vv_text(c, label, VV_STYLE(.fg = fg, .font_size = t->font_size - 3, .font = t->font));
  vv_end_box(c);
}

// Controlled: pass the current value, emit the new one in `.as_int`.
uint32_t shadcn_checkbox(vv_Ctx *c, const char *key, const char *label,
                         bool value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = {.ring_color = t->border_focus, .ring_width = 2,
                    .ring_offset = 2, .set = VV_STYLE_RING};
  uint32_t row = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER,
                .padding = vv_hv(2, 2), .focusable = true,
                .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.focus = &focus));
  vv_box_keyed(c, "box", 3,
               VV_LAYOUT(.w = vv_fixed(18), .h = vv_fixed(18),
                         .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = value ? t->brand_primary : t->surface_app,
                        .radius = vv_r(t->radius_sm),
                        .border_width = vv_all(t->border_width),
                        .border_color = value ? t->brand_primary : t->border_strong));
  if (value)
    vv_text(c, "\xe2\x9c\x93", // ✓
            VV_STYLE(.fg = t->text_on_brand, .font_size = t->font_size - 2, .font = t->font));
  vv_end_box(c);
  if (label)
    vv_text(c, label, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
  vv_end_box(c);
  if (vv_clicked(c, row) || vv_activated(c, row))
    vv_emit(c, change, vv_pi(!value));
  return row;
}

uint32_t shadcn_switch(vv_Ctx *c, const char *key, bool value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = {.ring_color = t->border_focus, .ring_width = 2,
                    .ring_offset = 2, .set = VV_STYLE_RING};
  uint32_t id = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = vv_fixed(40), .h = vv_fixed(22), .focusable = true,
                .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.bg = value ? t->brand_primary : t->control_bg_active,
               .radius = vv_r(t->radius_full), .focus = &focus));
  // Knob slides between the two ends; the FLIP spring animates the move (§14.3).
  vv_box_keyed(c, "knob", 4,
               VV_LAYOUT(.w = vv_fixed(16), .h = vv_fixed(16),
                         .has_absolute = true,
                         .absolute = vv_rect(value ? 21 : 3, 3, 16, 16)),
               VV_STYLE(.bg = t->knob, .radius = vv_r(8)));
  vv_end_box(c);
  vv_end_box(c);
  if (vv_clicked(c, id) || vv_activated(c, id))
    vv_emit(c, change, vv_pi(!value));
  return id;
}

void shadcn_label(vv_Ctx *c, const char *text) {
  const vv_Theme *t = vv_theme();
  vv_text(c, text, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size - 1, .font = t->font));
}

void shadcn_separator(vv_Ctx *c) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(c, NULL, 0, VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(1)),
               VV_STYLE(.bg = t->border_default));
  vv_end_box(c);
}

void shadcn_avatar(vv_Ctx *c, const char *key, const char *initials) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.w = vv_fixed(40), .h = vv_fixed(40),
                         .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = t->control_bg_rest, .radius = vv_r(t->radius_full)));
  vv_text(c, initials, VV_STYLE(.fg = t->text_secondary, .font_size = t->font_size - 1, .font = t->font));
  vv_end_box(c);
}

void shadcn_progress(vv_Ctx *c, const char *key, float frac) {
  const vv_Theme *t = vv_theme();
  frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
  const float H = 8.0f;
  uint32_t track =
      vv_box_keyed(c, key, strlen(key),
                   VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(H), .clip = true),
                   VV_STYLE(.bg = t->control_bg_active,
                            .radius = vv_r(t->radius_full)));
  // Position the fill absolutely against the track's measured width (last
  // frame's actual_rect — the standard §4.5 lag, same as vv_slider's fill).
  // vv_percent on the cross-axis child collapsed to nothing here; absolute
  // geometry is the reliable pattern for a fill bar.
  vv_Node *tn = vv_node(c, track);
  float tw = tn->actual_rect.w > 1.0f ? tn->actual_rect.w : 0.0f;
  vv_box_keyed(c, "bar", 3,
               VV_LAYOUT(.has_absolute = true,
                         .absolute = vv_rect(0, 0, tw * frac, H)),
               VV_STYLE(.bg = t->brand_primary, .radius = vv_r(t->radius_full)));
  vv_end_box(c);
  vv_end_box(c);
}

typedef enum { ALERT_DEFAULT, ALERT_DESTRUCTIVE } ShadcnAlertVariant;

void shadcn_alert(vv_Ctx *c, const char *key, const char *title,
                  const char *desc, ShadcnAlertVariant variant) {
  const vv_Theme *t = vv_theme();
  bool danger = variant == ALERT_DESTRUCTIVE;
  vv_Color bd = danger ? t->status_error : t->border_default;
  vv_Color head = danger ? t->status_error : t->text_primary;
  vv_Color bg = danger ? t->status_error_subtle : t->surface_card;
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 4,
                         .padding = vv_all(t->space_md)),
               VV_STYLE(.bg = bg, .radius = vv_r(t->radius_md),
                        .border_width = vv_all(t->border_width), .border_color = bd));
  vv_text(c, title, VV_STYLE(.fg = head, .font_size = t->font_size, .font = t->font));
  vv_text(c, desc, VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2, .font = t->font));
  vv_end_box(c);
}

// Card: a begin/end pair (it wraps arbitrary children, so it can't be a single
// call). Opens an elevated column; close with shadcn_card_end.
void shadcn_card_begin(vv_Ctx *c, const char *key) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(c, key, key ? strlen(key) : 0,
               VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1),
                         .padding = vv_all(t->space_lg), .gap = t->space_sm),
               VV_STYLE(.bg = t->surface_card, .radius = vv_r(t->radius_lg),
                        .border_width = vv_all(t->border_width),
                        .border_color = t->border_default,
                        .shadow = {.color = vv_rgba(0, 0, 0, 0.06f),
                                   .offset = vv_v2(0, 1), .blur = 3}));
}
void shadcn_card_end(vv_Ctx *c) { vv_end_box(c); }

// Tabs: a segmented control. The active tab is a raised "card" pill inside a
// muted track. Emits `change` with the tab index in .as_int.
uint32_t shadcn_tabs(vv_Ctx *c, const char *key, const char *const *labels,
                     int n, int active, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t bar = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW, .gap = 4, .padding = vv_all(4),
                .cross = VV_ALIGN_CENTER),
      VV_STYLE(.bg = t->control_bg_rest, .radius = vv_r(t->radius_md)));
  for (int i = 0; i < n; i++) {
    bool on = i == active;
    vv_Style hover = {.bg = t->control_bg_hover};
    uint32_t tab = vv_box_keyed(
        c, labels[i], strlen(labels[i]),
        VV_LAYOUT(.padding = vv_hv(14, 6), .main = VV_ALIGN_CENTER,
                  .cross = VV_ALIGN_CENTER, .focusable = true,
                  .cursor = VV_CURSOR_POINTER),
        VV_STYLE(.bg = on ? t->surface_card : vv_rgba(0, 0, 0, 0),
                 .radius = vv_r(t->radius_sm),
                 .shadow = on ? (vv_Shadow){.color = vv_rgba(0, 0, 0, 0.08f),
                                            .offset = vv_v2(0, 1), .blur = 2}
                              : (vv_Shadow){0},
                 .hover = on ? NULL : &hover));
    vv_text(c, labels[i],
            VV_STYLE(.fg = on ? t->text_primary : t->text_muted,
                     .font_size = t->font_size - 1, .font = t->font));
    vv_end_box(c);
    if (vv_clicked(c, tab) || vv_activated(c, tab)) vv_emit(c, change, vv_pi(i));
  }
  vv_end_box(c);
  return bar;
}

// Radio group item (controlled): emits `change` with `value` in .as_int.
uint32_t shadcn_radio(vv_Ctx *c, const char *key, const char *label,
                      bool selected, vv_Msg change, int value) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = {.ring_color = t->border_focus, .ring_width = 2,
                    .ring_offset = 2, .set = VV_STYLE_RING};
  uint32_t row = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER,
                .padding = vv_hv(2, 2), .focusable = true,
                .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.focus = &focus));
  vv_box_keyed(c, "o", 1,
               VV_LAYOUT(.w = vv_fixed(18), .h = vv_fixed(18),
                         .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = t->surface_app, .radius = vv_r(9),
                        .border_width = vv_all(t->border_width),
                        .border_color = selected ? t->brand_primary : t->border_strong));
  // Inner dot grows in from 0 when selected (FLIP spring).
  vv_box_keyed(c, "d", 1,
               VV_LAYOUT(.w = vv_fixed(selected ? 8 : 0),
                         .h = vv_fixed(selected ? 8 : 0)),
               VV_STYLE(.bg = t->brand_primary, .radius = vv_r(4)));
  vv_end_box(c);
  vv_end_box(c);
  if (label)
    vv_text(c, label, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
  vv_end_box(c);
  if (vv_clicked(c, row) || vv_activated(c, row)) vv_emit(c, change, vv_pi(value));
  return row;
}

// Slider (0..1, controlled). Emits `change` with the new value in .as_float.
uint32_t shadcn_slider(vv_Ctx *c, const char *key, float value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  value = value < 0 ? 0 : (value > 1 ? 1 : value);
  vv_Style focus = {.ring_color = t->border_focus, .ring_width = 2,
                    .ring_offset = 2, .set = VV_STYLE_RING};
  uint32_t track = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = {VV_SIZE_GROW, 1, 160, 0}, .h = vv_fixed(20),
                .cross = VV_ALIGN_CENTER, .focusable = true,
                .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.focus = &focus));
  vv_Node *tn = vv_node(c, track);
  float tw = tn->actual_rect.w > 1.0f ? tn->actual_rect.w : 160.0f;
  // Drag maps pointer x to value (last frame's geometry — the §4.5 lag).
  if (vv_active(c, track)) {
    float rel = (vv_mouse(c).x - tn->actual_rect.x) / tw;
    rel = rel < 0 ? 0 : (rel > 1 ? 1 : rel);
    if (rel != value) vv_emit(c, change, vv_pf(rel));
    value = rel;
  }
  const float H = 6.0f, K = 16.0f;
  // Rail (full width) + filled portion + knob, all absolutely placed so the
  // 20px hit-target stays put while the visuals are thin.
  vv_box_keyed(c, "rail", 4,
               VV_LAYOUT(.has_absolute = true, .absolute = vv_rect(0, 7, tw, H)),
               VV_STYLE(.bg = t->control_bg_active, .radius = vv_r(H * 0.5f)));
  vv_end_box(c);
  vv_box_keyed(c, "fill", 4,
               VV_LAYOUT(.has_absolute = true,
                         .absolute = vv_rect(0, 7, tw * value, H)),
               VV_STYLE(.bg = t->brand_primary, .radius = vv_r(H * 0.5f)));
  vv_end_box(c);
  vv_box_keyed(c, "knob", 4,
               VV_LAYOUT(.has_absolute = true,
                         .absolute = vv_rect(value * (tw - K), 2, K, K)),
               VV_STYLE(.bg = t->surface_app, .radius = vv_r(K * 0.5f),
                        .border_width = vv_all(2), .border_color = t->brand_primary));
  vv_end_box(c);
  vv_end_box(c);
  return track;
}

// Multiline textarea: the core editor in a shadcn-styled bordered card.
uint32_t shadcn_textarea(vv_Ctx *c, const char *key, char *buf, int cap,
                         const char *placeholder, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = {.border_color = t->border_focus};
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(96),
                         .padding = vv_all(2)),
               VV_STYLE(.bg = t->surface_app, .radius = vv_r(t->radius_md),
                        .border_width = vv_all(t->border_width),
                        .border_color = t->border_default, .focus = &focus));
  uint32_t ed = vv_text_area(c, "ed", buf, cap, 0, placeholder, change);
  vv_end_box(c);
  return ed;
}

// Toggle button: a pressable button that stays lit while `pressed`. Emits
// `change` with the flipped state in .as_int.
uint32_t shadcn_toggle_button(vv_Ctx *c, const char *key, const char *label,
                              bool pressed, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->control_bg_hover};
  vv_Style focus = {.ring_color = t->border_focus, .ring_width = 2,
                    .ring_offset = 2, .set = VV_STYLE_RING};
  uint32_t id = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.padding = vv_hv(12, 7), .main = VV_ALIGN_CENTER,
                .cross = VV_ALIGN_CENTER, .focusable = true,
                .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.bg = pressed ? t->control_bg_active : vv_rgba(0, 0, 0, 0),
               .radius = vv_r(t->radius_md), .hover = &hover, .focus = &focus));
  vv_text(c, label, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size - 1, .font = t->font));
  vv_end_box(c);
  if (vv_clicked(c, id) || vv_activated(c, id)) vv_emit(c, change, vv_pi(!pressed));
  return id;
}

// Skeleton: a muted placeholder block for loading states.
void shadcn_skeleton(vv_Ctx *c, const char *key, float w, float h) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.w = vv_fixed(w), .h = vv_fixed(h)),
               VV_STYLE(.bg = t->control_bg_rest, .radius = vv_r(t->radius_sm),
                        .opacity = 0.7f));
  vv_end_box(c);
}

// Select (combobox): a trigger showing the current option, opening a popover
// list. Open state is view-local (vv_ui_state), so no app plumbing; selection
// is controlled — pass `current`, emit the new index via `change`.
uint32_t shadcn_select(vv_Ctx *c, const char *key, const char *const *options,
                       int count, int current, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  bool *open = vv_ui_state(c, key, bool);
  vv_Style hover = {.bg = t->control_bg_hover};
  vv_Style focus = {.ring_color = t->border_focus, .ring_width = 2,
                    .ring_offset = 2, .set = VV_STYLE_RING};
  uint32_t trig = vv_box_keyed(
      c, key, strlen(key),
      VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(38),
                .cross = VV_ALIGN_CENTER, .main = VV_ALIGN_SPACE_BETWEEN,
                .padding = vv_hv(12, 0), .focusable = true, .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.bg = t->surface_app, .radius = vv_r(t->radius_md),
               .border_width = vv_all(t->border_width), .border_color = t->border_default,
               .hover = &hover, .focus = &focus));
  bool has = current >= 0 && current < count;
  vv_text(c, has ? options[current] : "Select\xe2\x80\xa6",
          VV_STYLE(.fg = has ? t->text_primary : t->text_muted, .font_size = t->font_size, .font = t->font));
  vv_text(c, "\xe2\x96\xbe", VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2, .font = t->font)); // ▾
  vv_end_box(c);
  if (vv_clicked(c, trig) || vv_activated(c, trig)) *open = !*open;
  if (*open) {
    vv_Rect r = vv_node(c, trig)->actual_rect;
    vv_popover_open(c, vv_fmt(c, "%s_pop", key), vv_v2(r.x, r.y + r.h + 4), r.w, open);
    for (int i = 0; i < count; i++) {
      vv_Style ih = {.bg = t->control_bg_hover};
      uint32_t oid = vv_box_keyed(
          c, options[i], strlen(options[i]),
          VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(30),
                    .cross = VV_ALIGN_CENTER, .main = VV_ALIGN_SPACE_BETWEEN,
                    .padding = vv_hv(8, 0), .focusable = true, .cursor = VV_CURSOR_POINTER),
          VV_STYLE(.bg = vv_rgba(0, 0, 0, 0), .radius = vv_r(t->radius_sm), .hover = &ih));
      vv_text(c, options[i], VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
      if (i == current)
        vv_text(c, "\xe2\x9c\x93", VV_STYLE(.fg = t->text_primary, .font_size = t->font_size - 2, .font = t->font));
      vv_end_box(c);
      if (vv_clicked(c, oid) || vv_activated(c, oid)) { vv_emit(c, change, vv_pi(i)); *open = false; }
    }
    vv_popover_end(c);
  }
  return trig;
}

// Dialog: a modal (scrim + centered panel, dismissed by outside-click/Escape)
// with a title and description header. Add body + footer between begin/end.
// Guard the whole call with your open flag in view().
void shadcn_dialog_begin(vv_Ctx *c, const char *key, const char *title,
                         const char *desc, float width, vv_Msg close) {
  const vv_Theme *t = vv_theme();
  vv_modal_begin(c, key, width, close);
  vv_text(c, title, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size + 3, .font = FONT_BOLD));
  if (desc) {
    // Single-span rich text so a long description *wraps* to the panel width —
    // a plain vv_text keeps its intrinsic (unwrapped) width and would overflow.
    vv_Span s = {.text = desc, .color = t->text_muted, .size = t->font_size};
    vv_rich_text(c, "dlg_desc", &s, 1);
  }
}
void shadcn_dialog_end(vv_Ctx *c) { vv_modal_end(c); }

// A right-aligned footer row for dialog actions.
void shadcn_dialog_footer_begin(vv_Ctx *c) {
  vv_box_keyed(c, "footer", 6,
               VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 8,
                         .main = VV_ALIGN_END, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = vv_rgba(0, 0, 0, 0)));
}
void shadcn_dialog_footer_end(vv_Ctx *c) { vv_end_box(c); }

// ===========================================================================
// The gallery.
// ===========================================================================

enum {
  MSG_TOGGLE_DARK = 1, MSG_CHECK_TERMS, MSG_CHECK_NEWS, MSG_SWITCH_NOTIFY,
  MSG_INPUT, MSG_PROG_DEC, MSG_PROG_INC, MSG_NOOP,
  MSG_TAB, MSG_RADIO, MSG_SLIDER, MSG_BIO, MSG_TB_BOLD, MSG_TB_ITALIC,
  MSG_SELECT, MSG_DLG_OPEN, MSG_DLG_CLOSE, MSG_DLG_CONFIRM,
};

typedef struct {
  bool     is_dark;
  vv_Theme light, dark;
  bool     terms, news, notify;
  char     input[64];
  float    progress;
  int      tab;
  int      plan;      // radio-group choice
  float    volume;    // slider 0..1
  char     bio[256];  // textarea
  bool     bold, italic; // toggle buttons
  int      framework; // select choice
  bool     dialog_open;
} App;

static void update(void *state, vv_Event ev) {
  App *a = state;
  switch (ev.msg) {
  case MSG_TOGGLE_DARK:
    a->is_dark = (bool)ev.data.as_int;
    vv_set_theme(a->is_dark ? &a->dark : &a->light);
    break;
  case MSG_CHECK_TERMS:   a->terms  = (bool)ev.data.as_int; break;
  case MSG_CHECK_NEWS:    a->news   = (bool)ev.data.as_int; break;
  case MSG_SWITCH_NOTIFY: a->notify = (bool)ev.data.as_int; break;
  case MSG_PROG_DEC:      a->progress -= 0.1f; if (a->progress < 0) a->progress = 0; break;
  case MSG_PROG_INC:      a->progress += 0.1f; if (a->progress > 1) a->progress = 1; break;
  case MSG_TAB:           a->tab    = (int)ev.data.as_int; break;
  case MSG_RADIO:         a->plan   = (int)ev.data.as_int; break;
  case MSG_SLIDER:        a->volume = (float)ev.data.as_float; break;
  case MSG_TB_BOLD:       a->bold   = (bool)ev.data.as_int; break;
  case MSG_TB_ITALIC:     a->italic = (bool)ev.data.as_int; break;
  case MSG_SELECT:        a->framework = (int)ev.data.as_int; break;
  case MSG_DLG_OPEN:      a->dialog_open = true; break;
  case MSG_DLG_CLOSE:     a->dialog_open = false; break;
  case MSG_DLG_CONFIRM:   a->dialog_open = false; a->terms = true; break;
  default: break; // MSG_INPUT/MSG_BIO edit buffers in place; buttons are MSG_NOOP
  }
}

// A titled section wrapper: a heading + a card holding the demo.
static void section_title(vv_Ctx *c, const char *title) {
  const vv_Theme *t = vv_theme();
  vv_text(c, title, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size + 4, .font = t->font));
}

static void view(vv_Ctx *c, void *state) {
  App *a = state;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .scroll_y = true, .clip = true),
         VV_STYLE(.bg = t->surface_app)) {

    // Sticky-ish header row.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .padding = vv_hv(24, 14),
                        .cross = VV_ALIGN_CENTER, .main = VV_ALIGN_SPACE_BETWEEN),
           VV_STYLE(.bg = t->surface_panel,
                    .border_width = (vv_Edges){0, 0, 0, 1}, .border_color = t->border_default)) {
      vv_text(c, "shadcn/ui \xc2\xb7 Verve", VV_STYLE(.fg = t->text_primary, .font_size = t->font_size + 8, .font = t->font));
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
        vv_text(c, a->is_dark ? "Dark" : "Light", VV_STYLE(.fg = t->text_muted, .font_size = t->font_size));
        shadcn_switch(c, "themesw", a->is_dark, MSG_TOGGLE_DARK);
      }
    }

    // Centered content column with comfortable max width.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .main = VV_ALIGN_CENTER,
                        .padding = vv_all(24)), VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = {VV_SIZE_GROW, 1, 0, 880}, .gap = 24),
             VV_STYLE(.bg = {0})) {

        // --- Buttons ---------------------------------------------------------
        section_title(c, "Buttons");
        shadcn_card_begin(c, "card_btn");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .wrap = true, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_button(c, "b1", "Default",     BTN_DEFAULT,     SZ_DEFAULT, false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "b2", "Secondary",   BTN_SECONDARY,   SZ_DEFAULT, false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "b3", "Destructive", BTN_DESTRUCTIVE, SZ_DEFAULT, false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "b4", "Outline",     BTN_OUTLINE,     SZ_DEFAULT, false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "b5", "Ghost",       BTN_GHOST,       SZ_DEFAULT, false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "b6", "Link",        BTN_LINK,        SZ_DEFAULT, false, MSG_NOOP, VV_NO_PAYLOAD);
        }
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .wrap = true, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_button(c, "s1", "Small",    BTN_DEFAULT, SZ_SM,      false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "s2", "Default",  BTN_DEFAULT, SZ_DEFAULT, false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "s3", "Large",    BTN_DEFAULT, SZ_LG,      false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "s4", "+",        BTN_OUTLINE, SZ_ICON,    false, MSG_NOOP, VV_NO_PAYLOAD);
          shadcn_button(c, "s5", "Disabled", BTN_DEFAULT, SZ_DEFAULT, true,  MSG_NOOP, VV_NO_PAYLOAD);
        }
        shadcn_card_end(c);

        // --- Badges ----------------------------------------------------------
        section_title(c, "Badges");
        shadcn_card_begin(c, "card_badge");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 8, .wrap = true, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_badge(c, "bd1", "Default",     BADGE_DEFAULT);
          shadcn_badge(c, "bd2", "Secondary",   BADGE_SECONDARY);
          shadcn_badge(c, "bd3", "Destructive", BADGE_DESTRUCTIVE);
          shadcn_badge(c, "bd4", "Outline",     BADGE_OUTLINE);
        }
        shadcn_card_end(c);

        // --- Form controls ---------------------------------------------------
        section_title(c, "Form controls");
        shadcn_card_begin(c, "card_form");
        shadcn_label(c, "Email");
        vv_text_field(c, "email", a->input, (int)sizeof a->input, "you@example.com", MSG_INPUT);
        shadcn_separator(c);
        shadcn_checkbox(c, "terms", "Accept terms and conditions", a->terms, MSG_CHECK_TERMS);
        shadcn_checkbox(c, "news",  "Subscribe to the newsletter",  a->news,  MSG_CHECK_NEWS);
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_switch(c, "notify", a->notify, MSG_SWITCH_NOTIFY);
          vv_text(c, "Push notifications", VV_STYLE(.fg = t->text_primary, .font_size = t->font_size));
        }
        shadcn_card_end(c);

        // --- Data display: avatar / progress ---------------------------------
        section_title(c, "Data display");
        shadcn_card_begin(c, "card_data");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 12, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_avatar(c, "av1", "VV");
          VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 2), VV_STYLE(.bg = {0})) {
            vv_text(c, "Verve UI", VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
            vv_text(c, "@verve", VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2, .font = t->font));
          }
        }
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER, .w = vv_grow(1)), VV_STYLE(.bg = {0})) {
          shadcn_button(c, "pd", "-", BTN_OUTLINE, SZ_ICON, false, MSG_PROG_DEC, VV_NO_PAYLOAD);
          VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
            shadcn_progress(c, "prog", a->progress);
          }
          shadcn_button(c, "pi", "+", BTN_OUTLINE, SZ_ICON, false, MSG_PROG_INC, VV_NO_PAYLOAD);
        }
        shadcn_card_end(c);

        // --- Tabs ------------------------------------------------------------
        section_title(c, "Tabs");
        shadcn_card_begin(c, "card_tabs");
        {
          static const char *const tabs[] = {"Account", "Password", "Team"};
          shadcn_tabs(c, "tabs", tabs, 3, a->tab, MSG_TAB);
          const char *body[] = {
              "Make changes to your account here.",
              "Change your password here.",
              "Manage who has access to this workspace."};
          vv_text(c, body[a->tab], VV_STYLE(.fg = t->text_muted, .font_size = t->font_size, .font = t->font));
        }
        shadcn_card_end(c);

        // --- Selection: radio group + slider + toggles -----------------------
        section_title(c, "Selection");
        shadcn_card_begin(c, "card_sel");
        shadcn_label(c, "Plan");
        shadcn_radio(c, "r0", "Free — for hobby projects",   a->plan == 0, MSG_RADIO, 0);
        shadcn_radio(c, "r1", "Pro — for growing teams",     a->plan == 1, MSG_RADIO, 1);
        shadcn_radio(c, "r2", "Enterprise — advanced needs", a->plan == 2, MSG_RADIO, 2);
        shadcn_separator(c);
        shadcn_label(c, "Volume");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 12, .cross = VV_ALIGN_CENTER, .w = vv_grow(1)), VV_STYLE(.bg = {0})) {
          shadcn_slider(c, "vol", a->volume, MSG_SLIDER);
          vv_text(c, vv_fmt(c, "%d%%", (int)(a->volume * 100 + 0.5f)),
                  VV_STYLE(.fg = t->text_muted, .font_size = t->font_size, .font = t->font));
        }
        shadcn_separator(c);
        shadcn_label(c, "Formatting");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 6, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_toggle_button(c, "tbB", "Bold",   a->bold,   MSG_TB_BOLD);
          shadcn_toggle_button(c, "tbI", "Italic", a->italic, MSG_TB_ITALIC);
        }
        shadcn_card_end(c);

        // --- Textarea --------------------------------------------------------
        section_title(c, "Textarea");
        shadcn_card_begin(c, "card_ta");
        shadcn_label(c, "Bio");
        shadcn_textarea(c, "bio", a->bio, (int)sizeof a->bio,
                        "Tell us a little about yourself…", MSG_BIO);
        shadcn_card_end(c);

        // --- Skeleton --------------------------------------------------------
        section_title(c, "Skeleton");
        shadcn_card_begin(c, "card_skel");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 12, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_skeleton(c, "sk_av", 40, 40);
          VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 8), VV_STYLE(.bg = {0})) {
            shadcn_skeleton(c, "sk_l1", 220, 12);
            shadcn_skeleton(c, "sk_l2", 160, 12);
          }
        }
        shadcn_card_end(c);

        // --- Overlays: select + dialog ---------------------------------------
        section_title(c, "Select & Dialog");
        shadcn_card_begin(c, "card_ov");
        shadcn_label(c, "Framework");
        {
          static const char *const fw[] = {"Next.js", "SvelteKit", "Astro", "Remix", "Nuxt"};
          VV_BOX(c, VV_LAYOUT(.w = vv_fixed(260)), VV_STYLE(.bg = {0})) {
            shadcn_select(c, "fw", fw, 5, a->framework, MSG_SELECT);
          }
        }
        shadcn_separator(c);
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          shadcn_button(c, "opendlg", "Open dialog", BTN_DEFAULT, SZ_DEFAULT, false, MSG_DLG_OPEN, VV_NO_PAYLOAD);
          vv_text(c, "Opens a modal with a scrim (click away or Esc to dismiss).",
                  VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 1, .font = t->font));
        }
        shadcn_card_end(c);

        // The dialog itself: z-lifted, declared inline, guarded by the flag.
        if (a->dialog_open) {
          shadcn_dialog_begin(c, "dlg", "Are you absolutely sure?",
                              "This action cannot be undone. This will permanently "
                              "delete your account and remove your data from our servers.",
                              440, MSG_DLG_CLOSE);
          shadcn_dialog_footer_begin(c);
          shadcn_button(c, "dcancel", "Cancel",  BTN_OUTLINE,     SZ_DEFAULT, false, MSG_DLG_CLOSE,   VV_NO_PAYLOAD);
          shadcn_button(c, "dok",     "Continue", BTN_DESTRUCTIVE, SZ_DEFAULT, false, MSG_DLG_CONFIRM, VV_NO_PAYLOAD);
          shadcn_dialog_footer_end(c);
          shadcn_dialog_end(c);
        }

        // --- Alerts ----------------------------------------------------------
        section_title(c, "Alerts");
        shadcn_alert(c, "al1", "Heads up!", "You can add components to your app using the CLI.", ALERT_DEFAULT);
        shadcn_alert(c, "al2", "Error", "Your session has expired. Please log in again.", ALERT_DESTRUCTIVE);

        // --- Rich text -------------------------------------------------------
        // vv_rich_text is a general widget: styled inline runs (own colour, size
        // and font face) that wrap together as one paragraph. Bold/italic use a
        // real bold/italic face (see demo_fonts); emphasis can also be pure
        // colour/size when you only have one face.
        section_title(c, "Rich text");
        shadcn_card_begin(c, "card_rich");
        vv_Span spans[] = {
            {.text = "Rich text ", .color = t->text_primary, .size = t->font_size},
            {.text = "wraps ", .color = t->text_primary, .size = t->font_size, .font = FONT_BOLD},
            {.text = "styled inline runs — ", .color = t->text_secondary, .size = t->font_size},
            {.text = "colours", .color = t->status_info, .size = t->font_size},
            {.text = ", ", .color = t->text_secondary, .size = t->font_size},
            {.text = "sizes", .color = t->text_primary, .size = t->font_size + 7},
            {.text = ", and ", .color = t->text_secondary, .size = t->font_size},
            {.text = "italics", .color = t->text_primary, .size = t->font_size, .font = FONT_ITALIC},
            {.text = " — together in a single paragraph that reflows to the width.",
             .color = t->text_secondary, .size = t->font_size},
        };
        vv_rich_text(c, "rt", spans, (int)(sizeof spans / sizeof spans[0]));
        shadcn_card_end(c);

        // tail spacer so the last card clears the window bottom when scrolled
        VV_BOX(c, VV_LAYOUT(.h = vv_fixed(8)), VV_STYLE(.bg = {0})) {}
      }
    }
  }
}

int main(void) {
  static App state;
  state.light = shadcn_light();
  state.dark = shadcn_dark();
  state.is_dark = false;
  state.progress = 0.6f; // state.input starts empty (static zero-init)
  state.volume = 0.65f;
  state.plan = 1;
  vv_set_theme(&state.light);

  return vv_app_run(&(vv_AppDesc){
      .title = "shadcn/ui \xc2\xb7 Verve",
      .width = 1040, .height = 780,
      .clear = hex(0xffffff),
      .fonts = demo_fonts(), // regular/bold/italic -> ids 0/1/2 for rich text
      .update = update, .view = view, .state = &state,
      .clipboard = true,
  });
}
