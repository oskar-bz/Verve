// fluent.c — a slice of Microsoft's Fluent UI 2 rebuilt on Verve, plus a
// gallery. Same pattern as shadcn.c: the look lives entirely in a vv_Theme of
// design tokens, and each component maps a variant/size enum onto those tokens.
// Swap the theme (the header switch) and everything reskins — and animates
// there for free, because a theme is just values (§7.1).
//
// Fluent's own accents vs. shadcn: the brand is Communication Blue, corners are
// tighter (4px), the focus indicator is a dark outer stroke, and text inputs
// carry the signature brand *underline* that appears on focus.
//
//   make fluent   ->   ./build/fluent
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <stdio.h>
#include <string.h>

enum { FONT_REGULAR = 0, FONT_BOLD = 1, FONT_ITALIC = 2 };

static const char *const *demo_fonts(void) {
  static const char *seg[] = {
      "/usr/share/fonts/TTF/segoeui.ttf",
      "/usr/share/fonts/TTF/segoeuib.ttf",
      "/usr/share/fonts/TTF/segoeuii.ttf", NULL};
  static const char *noto[] = {
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/noto/NotoSans-Bold.ttf",
      "/usr/share/fonts/noto/NotoSans-Italic.ttf", NULL};
  static const char *lib[] = {
      "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Italic.ttf", NULL};
  const char *const *sets[] = {seg, noto, lib};
  for (int i = 0; i < 3; i++) {
    FILE *f = fopen(sets[i][0], "rb");
    if (f) { fclose(f); return sets[i]; }
  }
  return NULL;
}

static vv_Color hex(unsigned rgb) {
  return vv_rgb((float)((rgb >> 16) & 0xff) / 255.0f,
                (float)((rgb >> 8) & 0xff) / 255.0f,
                (float)(rgb & 0xff) / 255.0f);
}

// ---------------------------------------------------------------------------
// Themes: Fluent 2 "web light/dark", expressed in Verve design tokens.
// ---------------------------------------------------------------------------
static vv_Theme fluent_light(void) {
  return vv_theme_complete((vv_Theme){
      .surface_app = hex(0xffffff), .surface_panel = hex(0xfafafa),
      .surface_card = hex(0xffffff), .surface_overlay = hex(0xffffff),
      .control_bg_rest = hex(0xffffff), .control_bg_hover = hex(0xf5f5f5),
      .control_bg_active = hex(0xe0e0e0), .control_bg_disabled = hex(0xf0f0f0),
      .text_primary = hex(0x242424), .text_secondary = hex(0x424242),
      .text_muted = hex(0x616161), .text_inverse = hex(0xffffff), .text_on_brand = hex(0xffffff),
      .border_subtle = hex(0xececec), .border_default = hex(0xd1d1d1),
      .border_strong = hex(0x8a8a8a), .border_focus = hex(0x242424),
      .brand_primary = hex(0x0f6cbd), .brand_hover = hex(0x115ea3),
      .brand_active = hex(0x0c3b5e), .brand_subtle = hex(0xebf3fc),
      .status_error = hex(0xc50f1f), .status_error_subtle = hex(0xfdf3f4),
      .status_warning = hex(0xf7630c), .status_success = hex(0x0e700e), .status_info = hex(0x0f6cbd),
      .radius = 4.0f, .border_width = 1.0f, .pad_x = 12.0f, .pad_y = 7.0f, .gap = 8.0f, .font_size = 14.0f,
  });
}

static vv_Theme fluent_dark(void) {
  return vv_theme_complete((vv_Theme){
      .surface_app = hex(0x292929), .surface_panel = hex(0x1f1f1f),
      .surface_card = hex(0x292929), .surface_overlay = hex(0x2b2b2b),
      .control_bg_rest = hex(0x292929), .control_bg_hover = hex(0x333333),
      .control_bg_active = hex(0x404040), .control_bg_disabled = hex(0x242424),
      .text_primary = hex(0xffffff), .text_secondary = hex(0xe0e0e0),
      .text_muted = hex(0xadadad), .text_inverse = hex(0x242424), .text_on_brand = hex(0xffffff),
      .border_subtle = hex(0x333333), .border_default = hex(0x525252),
      .border_strong = hex(0x757575), .border_focus = hex(0xffffff),
      .brand_primary = hex(0x479ef5), .brand_hover = hex(0x62abf5),
      .brand_active = hex(0x2886de), .brand_subtle = hex(0x08375c),
      .status_error = hex(0xdc626d), .status_error_subtle = hex(0x3b2325),
      .status_warning = hex(0xfbbc76), .status_success = hex(0x54b054), .status_info = hex(0x479ef5),
      .radius = 4.0f, .border_width = 1.0f, .pad_x = 12.0f, .pad_y = 7.0f, .gap = 8.0f, .font_size = 14.0f,
  });
}

// A dark outer focus stroke offset from the box — Fluent's signature ring.
static vv_Style fluent_ring(const vv_Theme *t) {
  return (vv_Style){.ring_color = t->border_focus, .ring_width = 2,
                    .ring_offset = 2, .set = VV_STYLE_RING};
}

// ===========================================================================
// Components.
// ===========================================================================

typedef enum {
  BTN_PRIMARY, BTN_SECONDARY, BTN_OUTLINE, BTN_SUBTLE, BTN_TRANSPARENT,
} FluentBtnVariant;
typedef enum { SZ_SMALL, SZ_MEDIUM, SZ_LARGE } FluentSize;

uint32_t fluent_button(vv_Ctx *c, const char *key, const char *label,
                       FluentBtnVariant variant, FluentSize size, bool disabled,
                       vv_Msg click, vv_Payload arg) {
  const vv_Theme *t = vv_theme();
  vv_Color bg = {0}, fg = t->text_primary, bd = {0}, hov = t->control_bg_hover;
  float bw = 0.0f;
  switch (variant) {
  case BTN_PRIMARY:     bg = t->brand_primary;   fg = t->text_on_brand; hov = t->brand_hover; break;
  case BTN_SECONDARY:   bg = t->control_bg_rest;  fg = t->text_primary;  hov = t->control_bg_hover; bd = t->border_default; bw = t->border_width; break;
  case BTN_OUTLINE:     bg = vv_rgba(0, 0, 0, 0); fg = t->text_primary;  hov = t->control_bg_hover; bd = t->border_default; bw = t->border_width; break;
  // Subtle/transparent rest on the hover colour at alpha 0 so the bg spring
  // ramps only alpha in (no dark-grey flash through {0}).
  case BTN_SUBTLE:      hov = t->control_bg_hover; bg = vv_rgba(hov.r, hov.g, hov.b, 0.0f); fg = t->text_primary; break;
  case BTN_TRANSPARENT: hov = t->control_bg_hover; bg = vv_rgba(hov.r, hov.g, hov.b, 0.0f); fg = t->brand_primary; break;
  }

  float px = 12, py = 7, fs = t->font_size;
  switch (size) {
  case SZ_SMALL: px = 8;  py = 4;  fs = t->font_size - 2; break;
  case SZ_LARGE: px = 16; py = 10; fs = t->font_size + 2; break;
  default: break;
  }

  vv_Style hover  = {.bg = hov};
  vv_Style active = {.bg = variant == BTN_PRIMARY ? t->brand_active : t->control_bg_active};
  vv_Style focus  = fluent_ring(t);
  uint32_t id = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = vv_fit(), .h = vv_fit(), .padding = vv_hv(px, py),
                .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER,
                .focusable = !disabled,
                .cursor = disabled ? VV_CURSOR_DEFAULT : VV_CURSOR_POINTER),
      VV_STYLE(.bg = bg, .radius = vv_r(t->radius_md), .border_color = bd,
               .border_width = vv_all(bw), .opacity = disabled ? 0.4f : 1.0f,
               .hover = disabled ? NULL : &hover,
               .active = disabled ? NULL : &active, .focus = &focus));
  vv_text(c, label, VV_STYLE(.fg = fg, .font_size = fs, .font = t->font));
  vv_end_box(c);
  if (!disabled && (vv_clicked(c, id) || vv_activated(c, id))) vv_emit(c, click, arg);
  return id;
}

typedef enum {
  BADGE_BRAND, BADGE_NEUTRAL, BADGE_DANGER, BADGE_SUCCESS, BADGE_WARNING,
} FluentBadgeVariant;

void fluent_badge(vv_Ctx *c, const char *key, const char *label,
                  FluentBadgeVariant variant) {
  const vv_Theme *t = vv_theme();
  vv_Color bg, fg = t->text_on_brand;
  switch (variant) {
  case BADGE_NEUTRAL: bg = t->control_bg_active; fg = t->text_primary; break;
  case BADGE_DANGER:  bg = t->status_error;   break;
  case BADGE_SUCCESS: bg = t->status_success; break;
  case BADGE_WARNING: bg = t->status_warning; break;
  default:            bg = t->brand_primary;  break;
  }
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.padding = vv_hv(8, 2), .main = VV_ALIGN_CENTER,
                         .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = bg, .radius = vv_r(t->radius_full)));
  vv_text(c, label, VV_STYLE(.fg = fg, .font_size = t->font_size - 3, .font = t->font));
  vv_end_box(c);
}

// Checkbox (controlled): emits `change` with the new state in .as_int.
uint32_t fluent_checkbox(vv_Ctx *c, const char *key, const char *label,
                         bool value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = fluent_ring(t);
  uint32_t row = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER,
                .padding = vv_hv(2, 2), .focusable = true, .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.focus = &focus));
  vv_box_keyed(c, "box", 3,
               VV_LAYOUT(.w = vv_fixed(18), .h = vv_fixed(18),
                         .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = value ? t->brand_primary : vv_rgba(0, 0, 0, 0),
                        .radius = vv_r(3), .border_width = vv_all(t->border_width),
                        .border_color = value ? t->brand_primary : t->border_strong));
  if (value)
    vv_text(c, "\xe2\x9c\x93", VV_STYLE(.fg = t->text_on_brand, .font_size = t->font_size - 2, .font = t->font));
  vv_end_box(c);
  if (label)
    vv_text(c, label, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
  vv_end_box(c);
  if (vv_clicked(c, row) || vv_activated(c, row)) vv_emit(c, change, vv_pi(!value));
  return row;
}

// Switch/Toggle (controlled). Fluent's is a pill with a ring-outline knob.
uint32_t fluent_switch(vv_Ctx *c, const char *key, bool value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = fluent_ring(t);
  uint32_t id = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = vv_fixed(40), .h = vv_fixed(20), .focusable = true,
                .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.bg = value ? t->brand_primary : vv_rgba(0, 0, 0, 0),
               .radius = vv_r(t->radius_full),
               .border_width = vv_all(value ? 0 : t->border_width),
               .border_color = t->border_strong, .focus = &focus));
  vv_box_keyed(c, "knob", 4,
               VV_LAYOUT(.w = vv_fixed(12), .h = vv_fixed(12), .has_absolute = true,
                         .absolute = vv_rect(value ? 24 : 5, 4, 12, 12)),
               VV_STYLE(.bg = value ? t->text_on_brand : t->text_muted,
                        .radius = vv_r(6)));
  vv_end_box(c);
  vv_end_box(c);
  if (vv_clicked(c, id) || vv_activated(c, id)) vv_emit(c, change, vv_pi(!value));
  return id;
}

// Radio-group item (controlled): emits `change` with `value` in .as_int.
uint32_t fluent_radio(vv_Ctx *c, const char *key, const char *label,
                      bool selected, vv_Msg change, int value) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = fluent_ring(t);
  uint32_t row = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER,
                .padding = vv_hv(2, 2), .focusable = true, .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.focus = &focus));
  vv_box_keyed(c, "o", 1,
               VV_LAYOUT(.w = vv_fixed(18), .h = vv_fixed(18),
                         .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = vv_rgba(0, 0, 0, 0), .radius = vv_r(9),
                        .border_width = vv_all(t->border_width),
                        .border_color = selected ? t->brand_primary : t->border_strong));
  vv_box_keyed(c, "d", 1,
               VV_LAYOUT(.w = vv_fixed(selected ? 8 : 0), .h = vv_fixed(selected ? 8 : 0)),
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
uint32_t fluent_slider(vv_Ctx *c, const char *key, float value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  value = value < 0 ? 0 : (value > 1 ? 1 : value);
  vv_Style focus = fluent_ring(t);
  uint32_t track = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = {VV_SIZE_GROW, 1, 160, 0}, .h = vv_fixed(20),
                .cross = VV_ALIGN_CENTER, .focusable = true, .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.focus = &focus));
  vv_Node *tn = vv_node(c, track);
  float tw = tn->actual_rect.w > 1.0f ? tn->actual_rect.w : 160.0f;
  if (vv_active(c, track)) {
    float rel = (vv_mouse(c).x - tn->actual_rect.x) / tw;
    rel = rel < 0 ? 0 : (rel > 1 ? 1 : rel);
    if (rel != value) vv_emit(c, change, vv_pf(rel));
    value = rel;
  }
  const float H = 4.0f, K = 16.0f;
  vv_box_keyed(c, "rail", 4, VV_LAYOUT(.has_absolute = true, .absolute = vv_rect(0, 8, tw, H)),
               VV_STYLE(.bg = t->control_bg_active, .radius = vv_r(H * 0.5f)));
  vv_end_box(c);
  vv_box_keyed(c, "fill", 4, VV_LAYOUT(.has_absolute = true, .absolute = vv_rect(0, 8, tw * value, H)),
               VV_STYLE(.bg = t->brand_primary, .radius = vv_r(H * 0.5f)));
  vv_end_box(c);
  // Fluent knob: a brand ring around a hollow centre.
  vv_box_keyed(c, "knob", 4,
               VV_LAYOUT(.has_absolute = true, .absolute = vv_rect(value * (tw - K), 2, K, K)),
               VV_STYLE(.bg = t->surface_card, .radius = vv_r(K * 0.5f),
                        .border_width = vv_all(4), .border_color = t->brand_primary));
  vv_end_box(c);
  vv_end_box(c);
  return track;
}

// Text input with Fluent's underline: a 1px bordered field whose *bottom* edge
// thickens to a brand accent on focus (a bar drawn under the field).
uint32_t fluent_input(vv_Ctx *c, const char *key, char *buf, int cap,
                      const char *placeholder, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t wrap = vv_box_keyed(
      c, key, strlen(key),
      VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1)),
      VV_STYLE(.bg = t->control_bg_rest, .radius = vv_r(t->radius_md),
               .border_width = vv_all(t->border_width), .border_color = t->border_default));
  bool focused = vv_focused(c, wrap);
  vv_text_field(c, "ed", buf, cap, placeholder, change);
  // Underline: rests as a 1px muted rule, springs to a 2px brand accent while
  // focused. Absolute so it overlays the field's bottom border without shifting.
  vv_Node *wn = vv_node(c, wrap);
  float w = wn->actual_rect.w, h = wn->actual_rect.h;
  vv_box_keyed(c, "ul", 2,
               VV_LAYOUT(.has_absolute = true,
                         .absolute = vv_rect(0, h - 2, w, 2)),
               VV_STYLE(.bg = focused ? t->brand_primary : vv_rgba(0, 0, 0, 0),
                        .radius = vv_r(1)));
  vv_end_box(c);
  vv_end_box(c);
  return wrap;
}

// Multiline textarea in a Fluent-bordered card.
uint32_t fluent_textarea(vv_Ctx *c, const char *key, char *buf, int cap,
                         const char *placeholder, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style focus = {.border_color = t->brand_primary};
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(96), .padding = vv_all(2)),
               VV_STYLE(.bg = t->control_bg_rest, .radius = vv_r(t->radius_md),
                        .border_width = vv_all(t->border_width),
                        .border_color = t->border_default, .focus = &focus));
  uint32_t ed = vv_text_area(c, "ed", buf, cap, 0, placeholder, change);
  vv_end_box(c);
  return ed;
}

// Pivot (tabs): labels with a brand underline under the active one. Emits
// `change` with the tab index in .as_int.
uint32_t fluent_pivot(vv_Ctx *c, const char *key, const char *const *labels,
                      int n, int active, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t bar = vv_box_keyed(
      c, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW, .gap = 4, .w = vv_grow(1)),
      VV_STYLE(.bg = vv_rgba(0, 0, 0, 0),
               .border_width = (vv_Edges){0, 0, 0, 1}, .border_color = t->border_subtle));
  for (int i = 0; i < n; i++) {
    bool on = i == active;
    vv_Style hover = {.bg = t->control_bg_hover};
    uint32_t tab = vv_box_keyed(
        c, labels[i], strlen(labels[i]),
        VV_LAYOUT(.dir = VV_COLUMN, .padding = vv_hv(10, 8), .gap = 6,
                  .cross = VV_ALIGN_CENTER, .focusable = true, .cursor = VV_CURSOR_POINTER),
        VV_STYLE(.bg = vv_rgba(0, 0, 0, 0), .radius = vv_r(t->radius_sm),
                 .hover = on ? NULL : &hover));
    vv_text(c, labels[i],
            VV_STYLE(.fg = on ? t->text_primary : t->text_muted,
                     .font_size = t->font_size, .font = t->font));
    // Underline indicator: brand under the active tab, transparent otherwise.
    vv_box_keyed(c, "ind", 3, VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(2)),
                 VV_STYLE(.bg = on ? t->brand_primary : vv_rgba(0, 0, 0, 0),
                          .radius = vv_r(1)));
    vv_end_box(c);
    vv_end_box(c);
    if (vv_clicked(c, tab) || vv_activated(c, tab)) vv_emit(c, change, vv_pi(i));
  }
  vv_end_box(c);
  return bar;
}

// Progress bar (0..1). Fluent's is a thin 2px brand fill in a muted track.
void fluent_progress(vv_Ctx *c, const char *key, float frac) {
  const vv_Theme *t = vv_theme();
  frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
  const float H = 3.0f;
  uint32_t track = vv_box_keyed(
      c, key, strlen(key), VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(H), .clip = true),
      VV_STYLE(.bg = t->control_bg_active, .radius = vv_r(H * 0.5f)));
  vv_Node *tn = vv_node(c, track);
  float tw = tn->actual_rect.w > 1.0f ? tn->actual_rect.w : 0.0f;
  vv_box_keyed(c, "bar", 3,
               VV_LAYOUT(.has_absolute = true, .absolute = vv_rect(0, 0, tw * frac, H)),
               VV_STYLE(.bg = t->brand_primary, .radius = vv_r(H * 0.5f)));
  vv_end_box(c);
  vv_end_box(c);
}

// Persona: an avatar circle with a coloured presence dot + name/secondary.
void fluent_persona(vv_Ctx *c, const char *key, const char *initials,
                    const char *name, const char *secondary) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
    uint32_t av = vv_box_keyed(
        c, key, strlen(key),
        VV_LAYOUT(.w = vv_fixed(40), .h = vv_fixed(40), .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
        VV_STYLE(.bg = t->brand_primary, .radius = vv_r(t->radius_full)));
    vv_text(c, initials, VV_STYLE(.fg = t->text_on_brand, .font_size = t->font_size - 1, .font = t->font));
    // Presence dot, lower-right, absolutely placed over the avatar.
    vv_box_keyed(c, "dot", 3,
                 VV_LAYOUT(.w = vv_fixed(11), .h = vv_fixed(11), .has_absolute = true,
                           .absolute = vv_rect(30, 30, 11, 11)),
                 VV_STYLE(.bg = t->status_success, .radius = vv_r(6),
                          .border_width = vv_all(2), .border_color = t->surface_card));
    vv_end_box(c);
    vv_end_box(c);
    (void)av;
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 1), VV_STYLE(.bg = {0})) {
      vv_text(c, name, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
      vv_text(c, secondary, VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2, .font = t->font));
    }
  }
}

typedef enum { MSG_INFO, MSG_SUCCESS, MSG_WARNING, MSG_ERROR } FluentIntent;

// MessageBar: a tinted bar with an accent edge and intent-coloured heading.
void fluent_message_bar(vv_Ctx *c, const char *key, const char *text,
                        FluentIntent intent) {
  const vv_Theme *t = vv_theme();
  vv_Color accent, tint;
  switch (intent) {
  case MSG_SUCCESS: accent = t->status_success; tint = t->brand_subtle; break;
  case MSG_WARNING: accent = t->status_warning; tint = t->surface_panel; break;
  case MSG_ERROR:   accent = t->status_error;   tint = t->status_error_subtle; break;
  default:          accent = t->status_info;    tint = t->brand_subtle; break;
  }
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 10,
                         .cross = VV_ALIGN_CENTER, .padding = vv_hv(12, 9)),
               VV_STYLE(.bg = tint, .radius = vv_r(t->radius_md),
                        .border_width = (vv_Edges){3, 0, 0, 0}, .border_color = accent));
  // A small filled dot as the intent glyph stand-in.
  vv_box_keyed(c, "dot", 3, VV_LAYOUT(.w = vv_fixed(8), .h = vv_fixed(8)),
               VV_STYLE(.bg = accent, .radius = vv_r(4)));
  vv_end_box(c);
  vv_text(c, text, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
  vv_end_box(c);
}

void fluent_divider(vv_Ctx *c) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(c, NULL, 0, VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(1)),
               VV_STYLE(.bg = t->border_subtle));
  vv_end_box(c);
}

// Card: a bordered, subtly-elevated surface. begin/end pair.
void fluent_card_begin(vv_Ctx *c, const char *key) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(c, key, key ? strlen(key) : 0,
               VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1),
                         .padding = vv_all(t->space_lg), .gap = t->space_sm),
               VV_STYLE(.bg = t->surface_card, .radius = vv_r(t->radius_lg),
                        .border_width = vv_all(t->border_width), .border_color = t->border_subtle,
                        .shadow = {.color = vv_rgba(0, 0, 0, 0.10f), .offset = vv_v2(0, 2), .blur = 6}));
}
void fluent_card_end(vv_Ctx *c) { vv_end_box(c); }

static void label_strong(vv_Ctx *c, const char *s) {
  const vv_Theme *t = vv_theme();
  vv_text(c, s, VV_STYLE(.fg = t->text_secondary, .font_size = t->font_size - 1, .font = FONT_BOLD));
}

// Dropdown (combobox): a trigger + popover list. Open state is view-local
// (vv_ui_state); selection is controlled — pass `current`, emit via `change`.
uint32_t fluent_dropdown(vv_Ctx *c, const char *key, const char *const *options,
                         int count, int current, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  bool *open = vv_ui_state(c, key, bool);
  vv_Style hover = {.bg = t->control_bg_hover};
  vv_Style focus = fluent_ring(t);
  uint32_t trig = vv_box_keyed(
      c, key, strlen(key),
      VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(32),
                .cross = VV_ALIGN_CENTER, .main = VV_ALIGN_SPACE_BETWEEN,
                .padding = vv_hv(10, 0), .focusable = true, .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.bg = t->control_bg_rest, .radius = vv_r(t->radius_md),
               .border_width = vv_all(t->border_width), .border_color = t->border_strong,
               .hover = &hover, .focus = &focus));
  bool has = current >= 0 && current < count;
  vv_text(c, has ? options[current] : "Select an option",
          VV_STYLE(.fg = has ? t->text_primary : t->text_muted, .font_size = t->font_size, .font = t->font));
  vv_text(c, "\xe2\x96\xbe", VV_STYLE(.fg = t->text_secondary, .font_size = t->font_size - 2, .font = t->font));
  vv_end_box(c);
  if (vv_clicked(c, trig) || vv_activated(c, trig)) *open = !*open;
  if (*open) {
    vv_Rect r = vv_node(c, trig)->actual_rect;
    vv_popover_open(c, vv_fmt(c, "%s_pop", key), vv_v2(r.x, r.y + r.h + 2), r.w, open);
    for (int i = 0; i < count; i++) {
      bool sel = i == current;
      vv_Style ih = {.bg = t->control_bg_hover};
      uint32_t oid = vv_box_keyed(
          c, options[i], strlen(options[i]),
          VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(32),
                    .cross = VV_ALIGN_CENTER, .gap = 8, .padding = vv_hv(8, 0),
                    .focusable = true, .cursor = VV_CURSOR_POINTER),
          VV_STYLE(.bg = sel ? t->brand_subtle : vv_rgba(0, 0, 0, 0),
                   .radius = vv_r(t->radius_sm), .hover = &ih));
      // Selected items carry a leading brand bar (Fluent's selection marker).
      vv_box_keyed(c, "m", 1, VV_LAYOUT(.w = vv_fixed(3), .h = vv_fixed(16)),
                   VV_STYLE(.bg = sel ? t->brand_primary : vv_rgba(0, 0, 0, 0), .radius = vv_r(2)));
      vv_end_box(c);
      vv_text(c, options[i],
              VV_STYLE(.fg = sel ? t->brand_primary : t->text_primary, .font_size = t->font_size, .font = t->font));
      vv_end_box(c);
      if (vv_clicked(c, oid) || vv_activated(c, oid)) { vv_emit(c, change, vv_pi(i)); *open = false; }
    }
    vv_popover_end(c);
  }
  return trig;
}

// Dialog: a modal (scrim + centered panel, dismissed by outside-click/Escape)
// with a title and body text. Add body + footer between begin/end; guard the
// whole call with your open flag in view().
void fluent_dialog_begin(vv_Ctx *c, const char *key, const char *title,
                         const char *body, float width, vv_Msg close) {
  const vv_Theme *t = vv_theme();
  vv_modal_begin(c, key, width, close);
  vv_text(c, title, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size + 6, .font = FONT_BOLD));
  if (body) {
    // Single-span rich text so a long body *wraps* to the panel width (a plain
    // vv_text keeps its intrinsic width and would overflow the panel).
    vv_Span s = {.text = body, .color = t->text_secondary, .size = t->font_size};
    vv_rich_text(c, "dlg_body", &s, 1);
  }
}
void fluent_dialog_end(vv_Ctx *c) { vv_modal_end(c); }

void fluent_dialog_footer_begin(vv_Ctx *c) {
  vv_box_keyed(c, "footer", 6,
               VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 8,
                         .main = VV_ALIGN_END, .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = vv_rgba(0, 0, 0, 0)));
}
void fluent_dialog_footer_end(vv_Ctx *c) { vv_end_box(c); }

// ===========================================================================
// The gallery.
// ===========================================================================

enum {
  MSG_TOGGLE_DARK = 1, MSG_CHECK_A, MSG_CHECK_B, MSG_SWITCH, MSG_INPUT,
  MSG_RADIO, MSG_SLIDER, MSG_BIO, MSG_TAB, MSG_PROG_DEC, MSG_PROG_INC, MSG_NOOP,
  MSG_SELECT, MSG_DLG_OPEN, MSG_DLG_CLOSE, MSG_DLG_CONFIRM,
};

typedef struct {
  bool     is_dark;
  vv_Theme light, dark;
  bool     check_a, check_b, sw;
  char     input[64];
  int      plan, tab;
  float    volume, progress;
  char     bio[256];
  int      region;      // dropdown choice
  bool     dialog_open;
} App;

static void update(void *state, vv_Event ev) {
  App *a = state;
  switch (ev.msg) {
  case MSG_TOGGLE_DARK:
    a->is_dark = (bool)ev.data.as_int;
    vv_set_theme(a->is_dark ? &a->dark : &a->light);
    break;
  case MSG_CHECK_A:  a->check_a = (bool)ev.data.as_int; break;
  case MSG_CHECK_B:  a->check_b = (bool)ev.data.as_int; break;
  case MSG_SWITCH:   a->sw     = (bool)ev.data.as_int; break;
  case MSG_RADIO:    a->plan   = (int)ev.data.as_int; break;
  case MSG_TAB:      a->tab    = (int)ev.data.as_int; break;
  case MSG_SLIDER:   a->volume = (float)ev.data.as_float; break;
  case MSG_PROG_DEC: a->progress -= 0.1f; if (a->progress < 0) a->progress = 0; break;
  case MSG_PROG_INC: a->progress += 0.1f; if (a->progress > 1) a->progress = 1; break;
  case MSG_SELECT:   a->region = (int)ev.data.as_int; break;
  case MSG_DLG_OPEN: a->dialog_open = true; break;
  case MSG_DLG_CLOSE: a->dialog_open = false; break;
  case MSG_DLG_CONFIRM: a->dialog_open = false; break;
  default: break; // MSG_INPUT/MSG_BIO edit buffers in place
  }
}

static void section(vv_Ctx *c, const char *title) {
  const vv_Theme *t = vv_theme();
  vv_text(c, title, VV_STYLE(.fg = t->text_primary, .font_size = t->font_size + 4, .font = FONT_BOLD));
}

static void view(vv_Ctx *c, void *state) {
  App *a = state;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .scroll_y = true, .clip = true),
         VV_STYLE(.bg = t->surface_app)) {

    // Header.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .padding = vv_hv(24, 14),
                        .cross = VV_ALIGN_CENTER, .main = VV_ALIGN_SPACE_BETWEEN),
           VV_STYLE(.bg = t->surface_panel,
                    .border_width = (vv_Edges){0, 0, 0, 1}, .border_color = t->border_subtle)) {
      vv_text(c, "Fluent UI 2 \xc2\xb7 Verve", VV_STYLE(.fg = t->text_primary, .font_size = t->font_size + 8, .font = FONT_BOLD));
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
        vv_text(c, a->is_dark ? "Dark" : "Light", VV_STYLE(.fg = t->text_muted, .font_size = t->font_size));
        fluent_switch(c, "themesw", a->is_dark, MSG_TOGGLE_DARK);
      }
    }

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .main = VV_ALIGN_CENTER, .padding = vv_all(24)),
           VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = {VV_SIZE_GROW, 1, 0, 880}, .gap = 24),
             VV_STYLE(.bg = {0})) {

        // Buttons.
        section(c, "Buttons");
        fluent_card_begin(c, "card_btn");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .wrap = true, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          fluent_button(c, "b1", "Primary",     BTN_PRIMARY,     SZ_MEDIUM, false, MSG_NOOP, VV_NO_PAYLOAD);
          fluent_button(c, "b2", "Secondary",   BTN_SECONDARY,   SZ_MEDIUM, false, MSG_NOOP, VV_NO_PAYLOAD);
          fluent_button(c, "b3", "Outline",     BTN_OUTLINE,     SZ_MEDIUM, false, MSG_NOOP, VV_NO_PAYLOAD);
          fluent_button(c, "b4", "Subtle",      BTN_SUBTLE,      SZ_MEDIUM, false, MSG_NOOP, VV_NO_PAYLOAD);
          fluent_button(c, "b5", "Transparent", BTN_TRANSPARENT, SZ_MEDIUM, false, MSG_NOOP, VV_NO_PAYLOAD);
        }
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .wrap = true, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          fluent_button(c, "z1", "Small",    BTN_PRIMARY, SZ_SMALL,  false, MSG_NOOP, VV_NO_PAYLOAD);
          fluent_button(c, "z2", "Medium",   BTN_PRIMARY, SZ_MEDIUM, false, MSG_NOOP, VV_NO_PAYLOAD);
          fluent_button(c, "z3", "Large",    BTN_PRIMARY, SZ_LARGE,  false, MSG_NOOP, VV_NO_PAYLOAD);
          fluent_button(c, "z4", "Disabled", BTN_SECONDARY, SZ_MEDIUM, true, MSG_NOOP, VV_NO_PAYLOAD);
        }
        fluent_card_end(c);

        // Badges.
        section(c, "Badges");
        fluent_card_begin(c, "card_badge");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 8, .wrap = true, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          fluent_badge(c, "g1", "Brand",   BADGE_BRAND);
          fluent_badge(c, "g2", "Neutral", BADGE_NEUTRAL);
          fluent_badge(c, "g3", "Success", BADGE_SUCCESS);
          fluent_badge(c, "g4", "Warning", BADGE_WARNING);
          fluent_badge(c, "g5", "Danger",  BADGE_DANGER);
        }
        fluent_card_end(c);

        // Pivot / tabs.
        section(c, "Pivot");
        fluent_card_begin(c, "card_pivot");
        {
          static const char *const tabs[] = {"Home", "Shared", "Recent", "Recycle bin"};
          fluent_pivot(c, "pivot", tabs, 4, a->tab, MSG_TAB);
          const char *body[] = {"Your files and folders.", "Files shared with you.",
                                "Recently opened items.", "Deleted items are kept here for 30 days."};
          vv_text(c, body[a->tab], VV_STYLE(.fg = t->text_muted, .font_size = t->font_size, .font = t->font));
        }
        fluent_card_end(c);

        // Form controls.
        section(c, "Form controls");
        fluent_card_begin(c, "card_form");
        label_strong(c, "Email");
        fluent_input(c, "email", a->input, (int)sizeof a->input, "you@example.com", MSG_INPUT);
        fluent_divider(c);
        fluent_checkbox(c, "ca", "Send me product updates", a->check_a, MSG_CHECK_A);
        fluent_checkbox(c, "cb", "Enable telemetry",        a->check_b, MSG_CHECK_B);
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          fluent_switch(c, "sw", a->sw, MSG_SWITCH);
          vv_text(c, a->sw ? "On" : "Off", VV_STYLE(.fg = t->text_primary, .font_size = t->font_size, .font = t->font));
        }
        fluent_card_end(c);

        // Selection.
        section(c, "Selection");
        fluent_card_begin(c, "card_sel");
        label_strong(c, "Subscription");
        fluent_radio(c, "r0", "Personal",   a->plan == 0, MSG_RADIO, 0);
        fluent_radio(c, "r1", "Business",    a->plan == 1, MSG_RADIO, 1);
        fluent_radio(c, "r2", "Enterprise",  a->plan == 2, MSG_RADIO, 2);
        fluent_divider(c);
        label_strong(c, "Volume");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 12, .cross = VV_ALIGN_CENTER, .w = vv_grow(1)), VV_STYLE(.bg = {0})) {
          fluent_slider(c, "vol", a->volume, MSG_SLIDER);
          vv_text(c, vv_fmt(c, "%d%%", (int)(a->volume * 100 + 0.5f)),
                  VV_STYLE(.fg = t->text_muted, .font_size = t->font_size, .font = t->font));
        }
        fluent_card_end(c);

        // Data display.
        section(c, "Data display");
        fluent_card_begin(c, "card_data");
        fluent_persona(c, "av", "AB", "Adele Vance", "adele@contoso.com");
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER, .w = vv_grow(1)), VV_STYLE(.bg = {0})) {
          fluent_button(c, "pd", "\xe2\x88\x92", BTN_SUBTLE, SZ_SMALL, false, MSG_PROG_DEC, VV_NO_PAYLOAD);
          VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
            fluent_progress(c, "prog", a->progress);
          }
          fluent_button(c, "pi", "+", BTN_SUBTLE, SZ_SMALL, false, MSG_PROG_INC, VV_NO_PAYLOAD);
        }
        fluent_card_end(c);

        // Textarea.
        section(c, "Textarea");
        fluent_card_begin(c, "card_ta");
        label_strong(c, "Notes");
        fluent_textarea(c, "bio", a->bio, (int)sizeof a->bio, "Type your notes…", MSG_BIO);
        fluent_card_end(c);

        // Dropdown + dialog.
        section(c, "Dropdown & Dialog");
        fluent_card_begin(c, "card_ov");
        label_strong(c, "Region");
        VV_BOX(c, VV_LAYOUT(.w = vv_fixed(260)), VV_STYLE(.bg = {0})) {
          static const char *const regions[] = {"West US", "East US", "West Europe", "Southeast Asia", "Australia East"};
          fluent_dropdown(c, "region", regions, 5, a->region, MSG_SELECT);
        }
        fluent_divider(c);
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
          fluent_button(c, "opendlg", "Delete item\xe2\x80\xa6", BTN_PRIMARY, SZ_MEDIUM, false, MSG_DLG_OPEN, VV_NO_PAYLOAD);
          vv_text(c, "Opens a dialog with a dimmed scrim (click away or Esc to dismiss).",
                  VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 1, .font = t->font));
        }
        fluent_card_end(c);

        if (a->dialog_open) {
          fluent_dialog_begin(c, "dlg", "Delete item?",
                             "This will permanently delete the item. You can't undo this action.",
                             460, MSG_DLG_CLOSE);
          fluent_dialog_footer_begin(c);
          fluent_button(c, "dok",     "Delete", BTN_PRIMARY,   SZ_MEDIUM, false, MSG_DLG_CONFIRM, VV_NO_PAYLOAD);
          fluent_button(c, "dcancel", "Cancel", BTN_SECONDARY, SZ_MEDIUM, false, MSG_DLG_CLOSE,   VV_NO_PAYLOAD);
          fluent_dialog_footer_end(c);
          fluent_dialog_end(c);
        }

        // Message bars.
        section(c, "Message bars");
        fluent_message_bar(c, "mb1", "Your changes were saved successfully.", MSG_SUCCESS);
        fluent_message_bar(c, "mb2", "A new version is available.", MSG_INFO);
        fluent_message_bar(c, "mb3", "Your storage is almost full.", MSG_WARNING);
        fluent_message_bar(c, "mb4", "We couldn't connect to the server.", MSG_ERROR);

        // Rich text.
        section(c, "Rich text");
        fluent_card_begin(c, "card_rich");
        vv_Span spans[] = {
            {.text = "Rich text ", .color = t->text_primary, .size = t->font_size},
            {.text = "supports ", .color = t->text_primary, .size = t->font_size, .font = FONT_BOLD},
            {.text = "inline runs with their own ", .color = t->text_secondary, .size = t->font_size},
            {.text = "colour", .color = t->status_info, .size = t->font_size},
            {.text = ", ", .color = t->text_secondary, .size = t->font_size},
            {.text = "size", .color = t->text_primary, .size = t->font_size + 7},
            {.text = ", and ", .color = t->text_secondary, .size = t->font_size},
            {.text = "italics", .color = t->text_primary, .size = t->font_size, .font = FONT_ITALIC},
            {.text = " — reflowing together as one paragraph.", .color = t->text_secondary, .size = t->font_size},
        };
        vv_rich_text(c, "rt", spans, (int)(sizeof spans / sizeof spans[0]));
        fluent_card_end(c);

        VV_BOX(c, VV_LAYOUT(.h = vv_fixed(8)), VV_STYLE(.bg = {0})) {}
      }
    }
  }
}

int main(void) {
  static App state;
  state.light = fluent_light();
  state.dark = fluent_dark();
  state.is_dark = false;
  state.plan = 1;
  state.volume = 0.65f;
  state.progress = 0.4f;
  vv_set_theme(&state.light);

  return vv_app_run(&(vv_AppDesc){
      .title = "Fluent UI 2 \xc2\xb7 Verve",
      .width = 1040, .height = 800,
      .clear = hex(0xffffff),
      .fonts = demo_fonts(),
      .update = update, .view = view, .state = &state,
      .clipboard = true,
  });
}
