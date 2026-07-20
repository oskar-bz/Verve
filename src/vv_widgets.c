#include "verve/vv_widgets.h"
#include "verve/vv_draw.h"
#include "verve/vv_layout.h"
#include "verve/vv_value.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Built-in widgets, each a function in the public API (§14.1). They lean on
// declarative variants for interaction styling so they animate for free and
// stay Present-tier under idle mode (§4.4). State (drag origin) uses vv_state.

static vv_Theme g_theme;
static bool g_theme_set;

vv_Theme vv_theme_dark(void) {
  return (vv_Theme){
      .surface = vv_rgb(0.14f, 0.15f, 0.18f),
      .surface_hi = vv_rgb(0.20f, 0.22f, 0.26f),
      .accent = vv_rgb(0.22f, 0.55f, 0.95f),
      .accent_hi = vv_rgb(0.35f, 0.65f, 1.00f),
      .accent_lo = vv_rgb(0.16f, 0.42f, 0.78f),
      .text = vv_rgb(0.92f, 0.93f, 0.95f),
      .text_muted = vv_rgb(0.55f, 0.58f, 0.63f),
      .on_accent = vv_rgb(1.00f, 1.00f, 1.00f),
      .track = vv_rgb(0.24f, 0.26f, 0.30f),
      .knob = vv_rgb(0.95f, 0.96f, 0.98f),
      .border = vv_rgb(0.30f, 0.32f, 0.37f),
      .danger = vv_rgb(0.90f, 0.35f, 0.30f),
      .radius = 8.0f,
      .font = 0,
      .font_size = 15.0f,
  };
}

void vv_set_theme(const vv_Theme *t) {
  g_theme = *t;
  g_theme_set = true;
}
const vv_Theme *vv_theme(void) {
  if (!g_theme_set) {
    g_theme = vv_theme_dark();
    g_theme_set = true;
  }
  return &g_theme;
}

// Bind auxiliary interactions on a just-built node. hover/press/double-click
// fire immediately from this frame's interaction flags; `move` is recorded on
// the node so the next input step can emit it while hovered (it can't be known
// here — motion is detected before the build). Payloads for these default to
// none, except move which carries the cursor. Usable on any box, not just the
// _on widget variants.
void vv_on(vv_Ctx *ctx, uint32_t id, vv_On on) {
  if (on.hover && vv_hovered(ctx, id))
    vv_emit(ctx, on.hover, VV_NO_PAYLOAD);
  if (on.press && vv_pressed(ctx, id))
    vv_emit(ctx, on.press, VV_NO_PAYLOAD);
  if (on.dbl && vv_double_clicked(ctx, id))
    vv_emit(ctx, on.dbl, VV_NO_PAYLOAD);
  vv_node(ctx, id)->on_move = on.move;
}

// ---- label ---------------------------------------------------------------

void vv_label(vv_Ctx *ctx, const char *text) {
  const vv_Theme *t = vv_theme();
  vv_text(
      ctx, text,
      VV_STYLE(.fg = t->text, .font_size = t->font_size, .font = t->font));
}
void vv_label_muted(vv_Ctx *ctx, const char *text) {
  const vv_Theme *t = vv_theme();
  vv_text(ctx, text,
          VV_STYLE(
              .fg = t->text_muted, .font_size = t->font_size, .font = t->font));
}

// ---- button --------------------------------------------------------------

uint32_t vv_button_on(vv_Ctx *ctx, const char *key, const char *label,
                      vv_Msg click, vv_Payload arg, vv_On on) {
  const vv_Theme *t = vv_theme();
  // Variants are consumed at build time (§7.1), so these locals are fine.
  vv_Style hover = {.bg = t->accent_hi};
  vv_Style active = {.bg = t->accent_lo, .transform = vv_scale(0.97f)};
  vv_Style focus = {.border_color = t->on_accent,
                    .border_width = vv_all(2)}; // keyboard ring
  uint32_t id = vv_box_keyed(
      ctx, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = vv_fit(), .h = vv_fixed(38), .padding = vv_hv(16, 9),
                .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER,
                .focusable = true, .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.bg = t->accent, .radius = vv_r(t->radius), .hover = &hover,
               .active = &active, .focus = &focus));

  vv_text(ctx, label,
          VV_STYLE(
              .fg = t->on_accent, .font_size = t->font_size, .font = t->font));
  vv_end_box(ctx);
  if (vv_clicked(ctx, id) || vv_activated(ctx, id))
    vv_emit(ctx, click, arg);
  vv_on(ctx, id, on);
  return id;
}

uint32_t vv_button(vv_Ctx *ctx, const char *key, const char *label,
                   vv_Msg click, vv_Payload arg) {
  return vv_button_on(ctx, key, label, click, arg, (vv_On){0});
}

// ---- toggle --------------------------------------------------------------
// The knob's x jumps between two positions; the FLIP spring slides it (§14.3).

uint32_t vv_toggle(vv_Ctx *ctx, const char *key, bool value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t id = vv_box_keyed(
      ctx, key, key ? strlen(key) : 0,
      VV_LAYOUT(.w = vv_fixed(46), .h = vv_fixed(26), .focusable = true),
      VV_STYLE(.bg = value ? t->accent : t->track, .radius = vv_r(13)));
  vv_box_keyed(ctx, "knob", 4,
               VV_LAYOUT(.w = vv_fixed(20), .h = vv_fixed(20),
                         .has_absolute = true,
                         .absolute = vv_rect(value ? 23 : 3, 3, 20, 20)),
               VV_STYLE(.bg = t->knob, .radius = vv_r(10)));
  vv_end_box(ctx);
  vv_end_box(ctx);
  if (vv_clicked(ctx, id) || vv_activated(ctx, id))
    vv_emit(ctx, change, vv_pi(!value));
  return id;
}

// ---- checkbox ------------------------------------------------------------

uint32_t vv_checkbox(vv_Ctx *ctx, const char *key, const char *label,
                     bool value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style cbfocus = {
      .border_color = t->accent, .border_width = vv_all(2), .radius = vv_r(6)};
  uint32_t row = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                              VV_LAYOUT(.dir = VV_ROW,
                                              .gap = 10,
                                              .cross = VV_ALIGN_CENTER,
                                              .padding = vv_hv(2, 2),
                                              .focusable = true),
                              VV_STYLE(.focus = &cbfocus));
  {
    vv_box_keyed(ctx, "box", 3,
                 VV_LAYOUT(.w = vv_fixed(20), .h = vv_fixed(20)),
                 VV_STYLE(.bg = value ? t->accent : t->surface_hi,
                            .radius = vv_r(5),
                            .border_width = vv_all(1),
                            .border_color = value ? t->accent : t->border));
    {
      if (value) {
        // A simple checkmark drawn as a small rotated bar pair via a box.
        vv_box_keyed(ctx, "tick", 9,
                     VV_LAYOUT(.w = vv_fixed(10),
                                     .h = vv_fixed(10),
                                     .has_absolute = true,
                                     .absolute = vv_rect(5, 5, 10, 10)),
                     VV_STYLE(.bg = t->on_accent, .radius = vv_r(2)));
        vv_end_box(ctx);
      }
    }
    vv_end_box(ctx);
    vv_label(ctx, label);
  }
  vv_end_box(ctx);
  if (vv_clicked(ctx, row))
    vv_emit(ctx, change, vv_pi(!value));
  return row;
}

// ---- slider --------------------------------------------------------------

typedef struct {
  bool was_active;
} SliderState;

typedef struct {
  uint32_t id;
  float value;
  bool changed, press, release;
} SliderResult;

// Shared slider core. Deals in value space, mapping pointer position through
// the perceptual curve (meta may be NULL => linear). Reports press/release
// edges so bound callers can bracket a transactional edit (§12.1).
static SliderResult slider_core(vv_Ctx *ctx, const char *key, float value,
                                float min, float max,
                                const vv_ValueMeta *meta) {
  const vv_Theme *t = vv_theme();
  float norm = vv_value_norm(meta, min, max, value);

  // A grow track with a real minimum so the slider keeps usable geometry even
  // inside a FIT parent (a bare row): with min 0 it would collapse to width 0,
  // the visuals would fall back to a fixed width, and the drag math below —
  // gated on actual width — would silently never fire (§8.2).
  uint32_t track = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                                VV_LAYOUT(.w = {VV_SIZE_GROW, 1, 160, 0},
                                                .h = vv_fixed(28),
                                                .focusable = true,
                                                .cross = VV_ALIGN_CENTER),
                                VV_STYLE(0));

  // Interaction: while active, map pointer x within the track to a value. Uses
  // last frame's track geometry (the §4.5 lag) — invisible at speed.
  float out = value;
  vv_Node *tn = vv_node(ctx, track);
  float tw = tn->actual_rect.w > 1.0f ? tn->actual_rect.w : 160.0f;
  bool active = vv_active(ctx, track);
  if (active) {
    float rel =
        vv_clampf((vv_mouse(ctx).x - tn->actual_rect.x) / tw, 0.0f, 1.0f);
    out = vv_value_denorm(meta, min, max, rel);
    norm = rel;
  }

  SliderState *st = vv_state(ctx, track, SliderState);
  SliderResult r = {.id = track,
                    .value = out,
                    .changed = out != value,
                    .press = vv_pressed(ctx, track),
                    .release = st->was_active && !active};
  st->was_active = active;

  float hx = norm * (tw - 18.0f);
  {
    // Rail.
    vv_box_keyed(ctx, "rail", 1,
                 VV_LAYOUT(.w = vv_grow(1),
                                 .h = vv_fixed(6),
                                 .has_absolute = true,
                                 .absolute = vv_rect(0, 11, tw, 6)),
                 VV_STYLE(.bg = t->track, .radius = vv_r(3)));
    vv_end_box(ctx);
    // Filled portion.
    vv_box_keyed(ctx, "fill", 2,
                 VV_LAYOUT(.has_absolute = true,
                                 .absolute = vv_rect(0, 11, hx + 9, 6)),
                 VV_STYLE(.bg = t->accent, .radius = vv_r(3)));
    vv_end_box(ctx);
    // Handle (springs along x via FLIP when value jumps).
    vv_Style hhover = {.bg = t->accent_hi};
    vv_box_keyed(ctx, "handle", 3,
                 VV_LAYOUT(.w = vv_fixed(18),
                                 .h = vv_fixed(18),
                                 .has_absolute = true,
                                 .absolute = vv_rect(hx, 5, 18, 18)),
                 VV_STYLE(.bg = t->knob,
                            .radius = vv_r(9),
                            .shadow = {.color = vv_rgba(0, 0, 0, 0.3f),
                                       .offset = vv_v2(0, 2),
                                       .blur = 6},
                            .hover = &hhover));
    vv_end_box(ctx);
  }
  vv_end_box(ctx);
  return r;
}

// Emit a value-binding event whose target is `v` (frame-arena record, valid
// until the next build when it is drained + applied, §12).
static void emit_bind(vv_Ctx *ctx, vv_Value v, vv_Payload val) {
  vv_BindEvent *b = vv_arena_alloc(&ctx->frame, sizeof *b);
  b->target = v;
  b->val = val;
  vv_emit(ctx, VV_MSG_BIND, vv_pp(b));
}

uint32_t vv_slider(vv_Ctx *ctx, const char *key, float value, float min,
                   float max, vv_Msg change) {
  SliderResult r = slider_core(ctx, key, value, min, max, NULL);
  if (r.changed)
    vv_emit(ctx, change, vv_pf(r.value));
  return r.id;
}

uint32_t vv_slider_bound(vv_Ctx *ctx, const char *key, vv_Value v) {
  float value = vv_value_as_float(v);
  float min = v.meta ? v.meta->min : 0.0f;
  float max = v.meta ? v.meta->max : 1.0f;
  bool readonly = v.meta && (v.meta->flags & VV_VAL_READONLY);
  SliderResult r = slider_core(ctx, key, value, min, max, v.meta);
  if (!readonly) {
    if (r.press)
      vv_begin_edit(ctx, v);
    if (r.changed)
      emit_bind(ctx, v, vv_pf(r.value));
    if (r.release)
      vv_end_edit(ctx, v);
  }
  return r.id;
}

// ---- drag_number ---------------------------------------------------------
// Drag-to-adjust, essential for parameter tweaking (§14.5). One undo session
// per drag would hook in here (§12.1) once the value registry lands.

typedef struct {
  float start;
  bool active;
} DragState;

static SliderResult drag_core(vv_Ctx *ctx, const char *key, float value,
                              float speed, float min, float max) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.w = vv_fixed(90),
                                             .h = vv_fixed(32),
                                             .padding = vv_hv(10, 6),
                                             .main = VV_ALIGN_CENTER,
                                             .cross = VV_ALIGN_CENTER,
                                             .focusable = true),
                             VV_STYLE(.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .hover = &hover));

  DragState *st = vv_state(ctx, id, DragState);
  float out = value;
  bool press = vv_pressed(ctx, id);
  if (press) {
    st->start = value;
    st->active = true;
  }
  if (st->active && vv_active(ctx, id)) {
    out = st->start + vv_drag_delta(ctx, id).x * speed;
    if (max > min)
      out = vv_clampf(out, min, max);
  }
  bool release = st->active && !vv_active(ctx, id);
  if (!vv_active(ctx, id))
    st->active = false;

  char buf[32];
  snprintf(buf, sizeof buf, "%.2f", (double)out);
  vv_text(
      ctx, buf,
      VV_STYLE(.fg = t->text, .font_size = t->font_size, .font = t->font));
  vv_end_box(ctx);
  return (SliderResult){.id = id,
                        .value = out,
                        .changed = out != value,
                        .press = press,
                        .release = release};
}

uint32_t vv_drag_number(vv_Ctx *ctx, const char *key, float value, float speed,
                        float min, float max, vv_Msg change) {
  SliderResult r = drag_core(ctx, key, value, speed, min, max);
  if (r.changed)
    vv_emit(ctx, change, vv_pf(r.value));
  return r.id;
}

uint32_t vv_drag_number_bound(vv_Ctx *ctx, const char *key, vv_Value v,
                              float speed) {
  float value = vv_value_as_float(v);
  float min = v.meta ? v.meta->min : 0.0f;
  float max = v.meta ? v.meta->max : 0.0f; // 0,0 => unclamped
  bool readonly = v.meta && (v.meta->flags & VV_VAL_READONLY);
  SliderResult r = drag_core(ctx, key, value, speed, min, max);
  if (!readonly) {
    if (r.press)
      vv_begin_edit(ctx, v);
    if (r.changed)
      emit_bind(ctx, v, vv_pf(r.value));
    if (r.release)
      vv_end_edit(ctx, v);
  }
  return r.id;
}

// ---- bound bools ----------------------------------------------------------
// Reuse the plain widgets for chrome (passing VV_MSG_NONE so they emit nothing)
// and translate a click into a bind event carrying the flipped value.

uint32_t vv_checkbox_bound(vv_Ctx *ctx, const char *key, const char *label,
                           vv_Value v) {
  bool value = v.ptr ? *(bool *)v.ptr : false;
  bool readonly = v.meta && (v.meta->flags & VV_VAL_READONLY);
  uint32_t id = vv_checkbox(ctx, key, label, value, VV_MSG_NONE);
  if (!readonly && vv_clicked(ctx, id))
    emit_bind(ctx, v, vv_pi(!value));
  return id;
}

uint32_t vv_toggle_bound(vv_Ctx *ctx, const char *key, vv_Value v) {
  bool value = v.ptr ? *(bool *)v.ptr : false;
  bool readonly = v.meta && (v.meta->flags & VV_VAL_READONLY);
  uint32_t id = vv_toggle(ctx, key, value, VV_MSG_NONE);
  if (!readonly && vv_clicked(ctx, id))
    emit_bind(ctx, v, vv_pi(!value));
  return id;
}

// ---- text field ----------------------------------------------------------
// Single-line editor. stb_textedit (§10.3) is the richer upgrade path; this
// hand-rolled version covers what visualizer UIs and the 7GUIs tasks need:
// insertion, deletion, arrow/home/end navigation, shift-selection, clipboard,
// and an animated caret (§10.3 — the glide reads as high quality).

typedef struct {
  int cursor, anchor; // byte offsets into buf
} TextFieldState;

static float measure_prefix(vv_Ctx *ctx, const char *buf, int n, float size) {
  if (n <= 0)
    return 0.0f;
  if (ctx->measure_text)
    return ctx->measure_text(ctx->measure_ud, buf, n, 0, size, 0).x;
  return (float)n * size * 0.5f;
}

// Byte index whose caret is nearest pointer x (relative to text start). Steps
// by whole codepoint so the caret lands on a character boundary, never
// mid-byte.
static int char_from_x(vv_Ctx *ctx, const char *buf, int len, float size,
                       float x) {
  if (x <= 0)
    return 0;
  float prev = 0;
  int i = 0;
  while (i < len) {
    int nxt = vv_utf8_next(buf, len, i);
    float w = measure_prefix(ctx, buf, nxt, size);
    if (x < (prev + w) * 0.5f)
      return i;
    prev = w;
    i = nxt;
  }
  return len;
}

static int sel_lo(TextFieldState *s) {
  return s->cursor < s->anchor ? s->cursor : s->anchor;
}
static int sel_hi(TextFieldState *s) {
  return s->cursor > s->anchor ? s->cursor : s->anchor;
}
static bool has_sel(TextFieldState *s) { return s->cursor != s->anchor; }

static void erase_range(char *buf, int *len, int lo, int hi) {
  memmove(buf + lo, buf + hi, (size_t)((int64_t)*len - hi + 1)); // include NUL
  *len -= (hi - lo);
}

static bool delete_selection(char *buf, int *len, TextFieldState *s) {
  if (!has_sel(s))
    return false;
  int lo = sel_lo(s), hi = sel_hi(s);
  erase_range(buf, len, lo, hi);
  s->cursor = s->anchor = lo;
  return true;
}

static void insert_text(char *buf, int *len, int cap, TextFieldState *s,
                        const char *ins, int ilen) {
  delete_selection(buf, len, s);
  if (*len + ilen >= cap)
    ilen = cap - 1 - *len;
  if (ilen <= 0)
    return;
  memmove(buf + s->cursor + ilen, buf + s->cursor,
          (size_t)((int64_t)*len - s->cursor + 1));
  memcpy(buf + s->cursor, ins, (size_t)ilen);
  *len += ilen;
  s->cursor += ilen;
  s->anchor = s->cursor;
}

// Copy the current selection to the clipboard (frame-arena scratch; the backend
// copies it). No-op if nothing is selected or no clipboard is bound.
static void clip_copy_selection(vv_Ctx *ctx, const char *buf,
                                TextFieldState *s) {
  if (!has_sel(s))
    return;
  int lo = sel_lo(s), hi = sel_hi(s), n = hi - lo;
  char *tmp = vv_arena_alloc(&ctx->frame, (size_t)n + 1);
  memcpy(tmp, buf + lo, (size_t)n);
  tmp[n] = 0;
  vv_clipboard_set(ctx, tmp);
}

// Paste clipboard text at the caret. `multiline` keeps newlines; otherwise they
// become spaces (a single-line field must stay one line). Returns true if it
// changed the buffer.
static bool clip_paste(vv_Ctx *ctx, char *buf, int *len, int cap,
                       TextFieldState *s, bool multiline) {
  const char *cb = vv_clipboard_get(ctx);
  if (!cb || !cb[0])
    return false;
  int n = (int)strlen(cb);
  char *tmp = vv_arena_alloc(&ctx->frame, (size_t)n + 1);
  for (int i = 0; i < n; i++)
    tmp[i] = (!multiline && (cb[i] == '\n' || cb[i] == '\r')) ? ' ' : cb[i];
  tmp[n] = 0;
  insert_text(buf, len, cap, s, tmp, n);
  return true;
}

// Returns true if the buffer changed.
static bool handle_key(vv_Ctx *ctx, char *buf, int *len, int cap,
                       TextFieldState *s, vv_KeyEvent ev) {
  bool changed = false;
  int prev = s->cursor;
  switch (ev.key) {
  case VV_KEY_LEFT:
    if (s->cursor > 0)
      s->cursor = vv_grapheme_prev(buf, *len, s->cursor);
    break;
  case VV_KEY_RIGHT:
    if (s->cursor < *len)
      s->cursor = vv_grapheme_next(buf, *len, s->cursor);
    break;
  case VV_KEY_HOME:
    s->cursor = 0;
    break;
  case VV_KEY_END:
    s->cursor = *len;
    break;
  case VV_KEY_BACKSPACE:
    if (delete_selection(buf, len, s)) {
      changed = true;
    } else if (ev.ctrl && s->cursor > 0) { // delete word before cursor
      int start = s->cursor;
      while (start > 0 && buf[start - 1] == ' ')
        start--;
      while (start > 0 && buf[start - 1] != ' ')
        start--;
      erase_range(buf, len, start, s->cursor);
      s->cursor = s->anchor = start;
      changed = true;
    } else if (s->cursor > 0) {
      int p =
          vv_grapheme_prev(buf, *len, s->cursor); // delete one whole codepoint
      erase_range(buf, len, p, s->cursor);
      s->cursor = p;
      s->anchor = s->cursor;
      changed = true;
    }
    break;
  case VV_KEY_DELETE:
    if (delete_selection(buf, len, s)) {
      changed = true;
    } else if (s->cursor < *len) {
      erase_range(buf, len, s->cursor, vv_grapheme_next(buf, *len, s->cursor));
      changed = true;
    }
    break;
  case VV_KEY_A:
    if (ev.ctrl) {
      s->anchor = 0;
      s->cursor = *len;
    }
    break;
  case VV_KEY_C:
    if (ev.ctrl)
      clip_copy_selection(ctx, buf, s);
    break;
  case VV_KEY_X:
    if (ev.ctrl && has_sel(s)) {
      clip_copy_selection(ctx, buf, s);
      delete_selection(buf, len, s);
      changed = true;
    }
    break;
  case VV_KEY_V:
    if (ev.ctrl)
      changed |= clip_paste(ctx, buf, len, cap, s, false);
    break;
  default:
    break;
  }
  if (ev.shift) { /* extend selection: keep anchor */
  } else if (ev.key == VV_KEY_LEFT || ev.key == VV_KEY_RIGHT ||
             ev.key == VV_KEY_HOME || ev.key == VV_KEY_END) {
    s->anchor = s->cursor; // collapse selection on plain move
  }
  (void)prev;
  (void)cap;
  return changed;
}

uint32_t vv_text_field(vv_Ctx *ctx, const char *key, char *buf, int cap,
                       const char *placeholder, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  int len = (int)strlen(buf);
  float size = t->font_size;

  vv_Style hover = {.bg = t->surface_hi};
  vv_Style focus = {.border_color = t->accent}; // declarative → animates
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.dir = VV_ROW,
                                             .w = vv_grow(1),
                                             .h = vv_fixed(34),
                                             .padding = vv_hv(10, 0),
                                             .cross = VV_ALIGN_CENTER,
                                             .focusable = true,
                                             .cursor = VV_CURSOR_TEXT,
                                             .clip = true),
                             VV_STYLE(.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .hover = &hover,
                                        .focus = &focus));

  bool focused = vv_focused(ctx, id);
  TextFieldState *s = vv_state(ctx, id, TextFieldState);
  bool changed = false;

  // Mouse: click positions the caret, drag extends the selection. Text starts
  // at the field's content origin (padding-left).
  vv_Rect fr = vv_node(ctx, id)->actual_rect;
  float text_x0 = fr.x + 10.0f;
  if (vv_pressed(ctx, id)) {
    vv_focus(ctx, id);
    s->cursor = s->anchor =
        char_from_x(ctx, buf, len, size, vv_mouse(ctx).x - text_x0);
  } else if (vv_active(ctx, id)) {
    s->cursor =
        char_from_x(ctx, buf, len, size, vv_mouse(ctx).x - text_x0); // extend
  }

  if (focused) {
    if (ctx->input.text_len > 0) {
      insert_text(buf, &len, cap, s, ctx->input.text, ctx->input.text_len);
      changed = true;
    }
    for (int i = 0; i < ctx->input.key_count; i++)
      changed |= handle_key(ctx, buf, &len, cap, s, ctx->input.keys[i]);
  }
  if (s->cursor > len)
    s->cursor = len;
  if (s->anchor > len)
    s->anchor = len;

  // Caret target position. The glide is the caret node's own FLIP spring,
  // driven by present every frame while unsettled — NOT a build-stepped spring,
  // which would freeze between rebuilds now that builds are gated (§4.2) and
  // leave the caret parked at a partial position.
  float cx = measure_prefix(ctx, buf, s->cursor, size);

  // Selection highlight (behind text).
  if (focused && has_sel(s)) {
    float lo = measure_prefix(ctx, buf, sel_lo(s), size);
    float hi = measure_prefix(ctx, buf, sel_hi(s), size);
    vv_box_keyed(
        ctx, "sel", 3,
        VV_LAYOUT(.has_absolute = true,
                        .absolute = vv_rect(lo, 6, hi - lo, 22)),
        VV_STYLE(.bg = vv_rgba(t->accent.r, t->accent.g, t->accent.b, 0.35f),
                   .radius = vv_r(3)));
    vv_end_box(ctx);
  }

  // Text or placeholder.
  if (len == 0 && placeholder && !focused)
    vv_text(
        ctx, placeholder,
        VV_STYLE(.fg = t->text_muted, .font_size = size, .font = t->font));
  else
    vv_text(ctx, buf,
            VV_STYLE(.fg = t->text, .font_size = size, .font = t->font));

  // Caret.
  if (focused) {
    vv_box_keyed(ctx, "caret", 7,
                 VV_LAYOUT(.w = vv_fixed(2),
                                 .h = vv_fixed(20),
                                 .has_absolute = true,
                                 .absolute = vv_rect(cx, 7, 2, 20)),
                 VV_STYLE(.bg = t->accent, .radius = vv_r(1)));
    vv_end_box(ctx);
  }

  // IME preedit: the in-progress composition, drawn (underlined) at the caret
  // but not committed to `buf` — SDL delivers it via vv_Input.preedit.
  if (focused && ctx->input.preedit_len > 0) {
    vv_box_keyed(ctx, "ime", 3,
                 VV_LAYOUT(.has_absolute = true,
                                 .absolute = vv_rect(cx + 2, 4, 0, 26)),
                 VV_STYLE(.bg = {0},
                            .border_width = (vv_Edges){0, 0, 0, 2},
                            .border_color = t->accent));
    vv_text(ctx, ctx->input.preedit,
            VV_STYLE(.fg = t->accent_hi, .font_size = size, .font = t->font));
    vv_end_box(ctx);
  }

  vv_end_box(ctx);
  if (changed)
    vv_emit(ctx, change, vv_ps(buf));
  return id;
}

// ---- list item -----------------------------------------------------------

uint32_t vv_list_item(vv_Ctx *ctx, const char *key, const char *label,
                      bool selected, vv_Msg click, vv_Payload arg) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  vv_Style focus = {.border_color = t->accent, .border_width = vv_all(2)};
  uint32_t id =
      vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                   VV_LAYOUT(.dir = VV_ROW,
                                   .w = vv_grow(1),
                                   .h = vv_fixed(34),
                                   .padding = vv_hv(12, 0),
                                   .cross = VV_ALIGN_CENTER,
                                   .focusable = true),
                   VV_STYLE(.bg = selected ? t->accent_lo : t->surface,
                              .radius = vv_r(6),
                              .hover = selected ? NULL : &hover,
                              .focus = &focus));
  vv_text(ctx, label,
          VV_STYLE(.fg = selected ? t->on_accent : t->text,
                     .font_size = t->font_size,
                     .font = t->font));
  vv_end_box(ctx);
  if (vv_clicked(ctx, id) || vv_activated(ctx, id))
    vv_emit(ctx, click, arg);
  return id;
}

// ---- multi-line text area -------------------------------------------------
// Reuses the single-line editor's byte-offset model (TextFieldState) and its
// edit helpers, adding line-aware navigation. Lines are split on '\n'; the
// caret column is measured with the same measure_prefix the backend draws with,
// so the caret and glyphs agree. line_h mirrors the backend's size * 1.25.

static int ml_line_start(const char *buf, int pos) {
  while (pos > 0 && buf[pos - 1] != '\n')
    pos--;
  return pos;
}
static int ml_line_end(const char *buf, int len, int pos) {
  while (pos < len && buf[pos] != '\n')
    pos++;
  return pos;
}
static int ml_line_index(const char *buf, int pos) {
  int line = 0;
  for (int i = 0; i < pos; i++)
    if (buf[i] == '\n')
      line++;
  return line;
}

// Move cursor up/down one line, aiming for pixel column `goal_x`.
static void ml_vmove(vv_Ctx *ctx, char *buf, int len, TextFieldState *s,
                     float size, float goal_x, int dir) {
  int ls = ml_line_start(buf, s->cursor);
  if (dir < 0) {
    if (ls == 0) {
      s->cursor = 0;
      return;
    }
    int prev_start = ml_line_start(buf, ls - 1);
    int prev_end = ls - 1;
    int col =
        char_from_x(ctx, buf + prev_start, prev_end - prev_start, size, goal_x);
    s->cursor = prev_start + col;
  } else {
    int le = ml_line_end(buf, len, s->cursor);
    if (le >= len) {
      s->cursor = len;
      return;
    }
    int next_start = le + 1;
    int next_end = ml_line_end(buf, len, next_start);
    int col =
        char_from_x(ctx, buf + next_start, next_end - next_start, size, goal_x);
    s->cursor = next_start + col;
  }
}

typedef struct {
  int cursor, anchor;
  float goal_x;
  bool have_goal;
} TextAreaState;

static bool ml_handle_key(vv_Ctx *ctx, char *buf, int *len, int cap,
                          TextAreaState *s, vv_KeyEvent ev, float size) {
  bool changed = false;
  TextFieldState *ts = (TextFieldState *)s; // cursor/anchor share layout
  bool vertical = (ev.key == VV_KEY_UP || ev.key == VV_KEY_DOWN);
  if (!vertical)
    s->have_goal = false;
  switch (ev.key) {
  case VV_KEY_LEFT:
    if (ts->cursor > 0)
      ts->cursor = vv_grapheme_prev(buf, *len, ts->cursor);
    break;
  case VV_KEY_RIGHT:
    if (ts->cursor < *len)
      ts->cursor = vv_grapheme_next(buf, *len, ts->cursor);
    break;
  case VV_KEY_HOME:
    ts->cursor = ml_line_start(buf, ts->cursor);
    break;
  case VV_KEY_END:
    ts->cursor = ml_line_end(buf, *len, ts->cursor);
    break;
  case VV_KEY_UP:
  case VV_KEY_DOWN: {
    if (!s->have_goal) {
      int ls = ml_line_start(buf, ts->cursor);
      s->goal_x = measure_prefix(ctx, buf + ls, ts->cursor - ls, size);
      s->have_goal = true;
    }
    ml_vmove(ctx, buf, *len, ts, size, s->goal_x, ev.key == VV_KEY_UP ? -1 : 1);
    break;
  }
  case VV_KEY_ENTER: {
    char nl = '\n';
    insert_text(buf, len, cap, ts, &nl, 1);
    changed = true;
    break;
  }
  case VV_KEY_BACKSPACE:
    if (delete_selection(buf, len, ts))
      changed = true;
    else if (ts->cursor > 0) {
      int p = vv_grapheme_prev(buf, *len, ts->cursor);
      erase_range(buf, len, p, ts->cursor);
      ts->cursor = p;
      ts->anchor = p;
      changed = true;
    }
    break;
  case VV_KEY_DELETE:
    if (delete_selection(buf, len, ts))
      changed = true;
    else if (ts->cursor < *len) {
      erase_range(buf, len, ts->cursor,
                  vv_grapheme_next(buf, *len, ts->cursor));
      changed = true;
    }
    break;
  case VV_KEY_A:
    if (ev.ctrl) {
      ts->anchor = 0;
      ts->cursor = *len;
    }
    break;
  case VV_KEY_C:
    if (ev.ctrl)
      clip_copy_selection(ctx, buf, ts);
    break;
  case VV_KEY_X:
    if (ev.ctrl && has_sel(ts)) {
      clip_copy_selection(ctx, buf, ts);
      delete_selection(buf, len, ts);
      changed = true;
    }
    break;
  case VV_KEY_V:
    if (ev.ctrl) {
      changed |= clip_paste(ctx, buf, len, cap, ts, true);
      s->have_goal = false;
    }
    break;
  default:
    break;
  }
  if (ev.shift) { /* extend: keep anchor */
  } else if (ev.key == VV_KEY_LEFT || ev.key == VV_KEY_RIGHT ||
             ev.key == VV_KEY_HOME || ev.key == VV_KEY_END ||
             ev.key == VV_KEY_UP || ev.key == VV_KEY_DOWN)
    ts->anchor = ts->cursor;
  return changed;
}

uint32_t vv_text_area(vv_Ctx *ctx, const char *key, char *buf, int cap,
                      float height, const char *placeholder, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  int len = (int)strlen(buf);
  float size = t->font_size;
  float line_h = size * 1.25f;
  float pad = 10.0f;

  vv_Style focus = {.border_color = t->accent};
  // height <= 0 means "grow to fill the parent" (a full-pane editor).
  vv_Size vh = height > 0 ? vv_fixed(height) : vv_grow(1);
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.dir = VV_COLUMN,
                                             .w = vv_grow(1),
                                             .h = vh,
                                             .padding = vv_all(pad),
                                             .focusable = true,
                                             .cursor = VV_CURSOR_TEXT,
                                             .clip = true,
                                             .scroll_y = true),
                             VV_STYLE(.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .focus = &focus));
  bool focused = vv_focused(ctx, id);
  TextAreaState *s = vv_state(ctx, id, TextAreaState);
  TextFieldState *ts = (TextFieldState *)s;
  bool changed = false;

  vv_Rect fr = vv_node(ctx, id)->actual_rect;
  float text_x0 = fr.x + pad;
  float text_y0 =
      fr.y + pad; // scroll offset is small; approximate hit for click
  // Map a mouse point to a caret index: pick the visual line, then the column.
  // Shared by press (sets caret + anchor) and drag (extends the selection).
  if (vv_pressed(ctx, id) || vv_active(ctx, id)) {
    int line = (int)((vv_mouse(ctx).y - text_y0) / line_h);
    if (line < 0)
      line = 0;
    int p = 0, cur = 0;
    while (cur < line && p < len) {
      if (buf[p] == '\n')
        cur++;
      p++;
    }
    int ls = p, le = ml_line_end(buf, len, p);
    int caret = ls + char_from_x(ctx, buf + ls, le - ls, size,
                                 vv_mouse(ctx).x - text_x0);
    if (vv_pressed(ctx, id)) {
      vv_focus(ctx, id);
      ts->cursor = ts->anchor = caret; // new selection
    } else {
      ts->cursor = caret; // drag: extend from the fixed anchor
    }
    s->have_goal = false;
  }

  if (focused) {
    if (ctx->input.text_len > 0) {
      insert_text(buf, &len, cap, ts, ctx->input.text, ctx->input.text_len);
      s->have_goal = false;
      changed = true;
    }
    for (int i = 0; i < ctx->input.key_count; i++)
      changed |=
          ml_handle_key(ctx, buf, &len, cap, s, ctx->input.keys[i], size);
  }
  if (ts->cursor > len)
    ts->cursor = len;
  if (ts->anchor > len)
    ts->anchor = len;

  // Selection: one highlight rect per spanned line (behind the text).
  if (focused && has_sel(ts)) {
    int lo = sel_lo(ts), hi = sel_hi(ts);
    int p = lo;
    while (p <= hi) {
      int ls = ml_line_start(buf, p);
      int le = ml_line_end(buf, len, p);
      int a = p > lo ? ls : lo;  // first spanned line starts at lo
      int b = le < hi ? le : hi; // last spanned line ends at hi
      float ax = measure_prefix(ctx, buf + ls, a - ls, size);
      float bx = measure_prefix(ctx, buf + ls, b - ls, size);
      float extra = (le < hi) ? 4.0f : 0.0f; // hint the newline is included
      float ly = (float)ml_line_index(buf, ls) * line_h;
      vv_box_keyed(
          ctx, vv_fmt(ctx, "sel%d", ls), 0,
          VV_LAYOUT(
              .has_absolute = true,
              // absolute is content-relative (post-padding), so no extra pad
              .absolute = vv_rect(ax, ly, bx - ax + extra, line_h)),
          VV_STYLE(.bg =
                         vv_rgba(t->accent.r, t->accent.g, t->accent.b, 0.30f),
                     .radius = vv_r(2)));
      vv_end_box(ctx);
      if (le >= len)
        break;
      p = le + 1;
    }
  }

  if (len == 0 && placeholder && !focused)
    vv_text(
        ctx, placeholder,
        VV_STYLE(.fg = t->text_muted, .font_size = size, .font = t->font));
  else
    vv_text(ctx, buf,
            VV_STYLE(.fg = t->text, .font_size = size, .font = t->font));

  // Caret at (column x, line y).
  if (focused) {
    int ls = ml_line_start(buf, ts->cursor);
    float cx = measure_prefix(ctx, buf + ls, ts->cursor - ls, size);
    float cy = (float)ml_line_index(buf, ts->cursor) * line_h;
    vv_box_keyed(ctx, "caret", 7,
                 VV_LAYOUT(.w = vv_fixed(2),
                                 .h = vv_fixed(line_h),
                                 .has_absolute = true,
                                 .absolute = vv_rect(cx, cy, 2, line_h)),
                 VV_STYLE(.bg = t->accent, .radius = vv_r(1)));
    vv_end_box(ctx);
    if (ctx->input.preedit_len > 0) { // IME composition at the caret
      vv_box_keyed(ctx, "ime", 3,
                   VV_LAYOUT(.has_absolute = true,
                                   .absolute = vv_rect(cx + 2, cy, 0, line_h),
                                   .cross = VV_ALIGN_CENTER),
                   VV_STYLE(.bg = {0},
                              .border_width = (vv_Edges){0, 0, 0, 2},
                              .border_color = t->accent));
      vv_text(
          ctx, ctx->input.preedit,
          VV_STYLE(.fg = t->accent_hi, .font_size = size, .font = t->font));
      vv_end_box(ctx);
    }
  }

  vv_end_box(ctx);
  if (changed)
    vv_emit(ctx, change, vv_ps(buf));
  return id;
}

// ---- splitter -------------------------------------------------------------
// Because view() is pure, the split size lives in app state; the splitter only
// reports drags. We capture the size at grab (per-node vv_state) and emit
// grab_size + drag_delta so tracking is 1:1 regardless of pointer acceleration.

uint32_t vv_splitter(vv_Ctx *ctx, const char *key, vv_Axis dir, bool trailing,
                     float size, float min, float max, vv_Msg resize) {
  const vv_Theme *t = vv_theme();
  bool horiz = (dir == VV_ROW);
  vv_Style hover = {.bg = t->accent};
  vv_Style active = {.bg = t->accent_hi};
  vv_LayoutDecl d = horiz ? VV_LAYOUT(.w = vv_fixed(6),
                                            .h = vv_grow(1),
                                            .focusable = true,
                                            .cursor = VV_CURSOR_RESIZE_H)
                          : VV_LAYOUT(.w = vv_grow(1),
                                            .h = vv_fixed(6),
                                            .focusable = true,
                                            .cursor = VV_CURSOR_RESIZE_V);
  uint32_t id = vv_box_keyed(
      ctx, key, strlen(key), d,
      VV_STYLE(.bg = t->border, .hover = &hover, .active = &active));
  float *grab = vv_state(ctx, id, float);
  if (vv_pressed(ctx, id))
    *grab = size;
  if (vv_active(ctx, id)) {
    vv_Vec2 dd = vv_drag_delta(ctx, id);
    float along = horiz ? dd.x : dd.y;
    // A trailing pane (right/bottom dock) shrinks as the divider moves toward
    // it.
    float v = *grab + (trailing ? -along : along);
    if (v < min)
      v = min;
    if (v > max)
      v = max;
    vv_emit(ctx, resize, vv_pf(v));
  }
  vv_end_box(ctx);
  return id;
}

// ---- overlay chrome: menus, popovers, tooltips ----------------------------
// Overlays paint on top only if built last in the tree (the painter is strict
// tree order). The menu system self-manages which menu is open via one static —
// menus are transient and mutually exclusive, so this stays simple.

static vv_ID g_open_menu; // node id of the open menu title, 0 = none
static bool
    *g_ctxmenu_open; // active context menu's open flag (vv_menu_item clears it)

void vv_menubar_begin(vv_Ctx *ctx) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(ctx, "__menubar", 9,
               VV_LAYOUT(.dir = VV_ROW,
                               .w = vv_grow(1),
                               .h = vv_fixed(34),
                               .cross = VV_ALIGN_CENTER,
                               .padding = vv_hv(6, 0),
                               .gap = 2),
               VV_STYLE(.bg = t->surface,
                          .border_width = (vv_Edges){0, 0, 0, 1},
                          .border_color = t->border));
}
void vv_menubar_end(vv_Ctx *ctx) { vv_end_box(ctx); }

uint32_t vv_menu_title(vv_Ctx *ctx, const char *key, const char *label) {
  const vv_Theme *t = vv_theme();
  // Resolve open state before styling: needs this node's stable id, which we
  // get from the handle after opening. Open one frame late is invisible here.
  uint32_t id = vv_box_keyed(ctx, key, strlen(key),
                             VV_LAYOUT(.dir = VV_ROW,
                                             .h = vv_fixed(24),
                                             .cross = VV_ALIGN_CENTER,
                                             .padding = vv_hv(10, 0),
                                             .focusable = true),
                             VV_STYLE(.radius = vv_r(6)));
  vv_ID nid = vv_node(ctx, id)->id;
  bool open = (g_open_menu == nid);
  if (vv_clicked(ctx, id) || vv_activated(ctx, id))
    g_open_menu = open ? 0 : nid;
  else if (g_open_menu != 0 && g_open_menu != nid && vv_hovered(ctx, id))
    g_open_menu = nid;
  // Re-read after the possible toggle so the highlight is immediate.
  open = (g_open_menu == nid);
  vv_Node *n = vv_node(ctx, id);
  n->target.bg = (open || vv_hovered(ctx, id)) ? t->surface_hi : (vv_Color){0};
  vv_text(
      ctx, label,
      VV_STYLE(.fg = t->text, .font_size = t->font_size, .font = t->font));
  vv_end_box(ctx);
  return id;
}

bool vv_menu_is_open(vv_Ctx *ctx, uint32_t title_id) {
  return vv_node(ctx, title_id)->id == g_open_menu;
}

void vv_menu_begin(vv_Ctx *ctx, const char *key, vv_Vec2 at) {
  const vv_Theme *t = vv_theme();
  // Scrim: full-window catch so a click or Escape anywhere else dismisses.
  uint32_t scrim = vv_box_keyed(
      ctx, "__menuscrim", 11,
      VV_LAYOUT(.has_absolute = true,
                      .z = VV_Z_MENU,
                      .absolute = vv_rect(0, 0, ctx->win_w, ctx->win_h)),
      VV_STYLE(.bg = {0}));
  if (vv_pressed(ctx, scrim))
    g_open_menu = 0;
  vv_end_box(ctx);
  for (int i = 0; i < ctx->input.key_count; i++)
    if (ctx->input.keys[i].key == VV_KEY_ESCAPE)
      g_open_menu = 0;

  vv_box_keyed(ctx, key, strlen(key),
               VV_LAYOUT(.dir = VV_COLUMN,
                               .w = vv_fixed(220),
                               .padding = vv_all(5),
                               .gap = 1,
                               .has_absolute = true,
                               .z = VV_Z_MENU,
                               .absolute = vv_rect(at.x, at.y + 2, 220, 0)),
               VV_STYLE(.bg = t->surface_hi,
                          .radius = vv_r(8),
                          .border_width = vv_all(1),
                          .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                                     .offset = vv_v2(0, 6),
                                     .blur = 18,
                                     .spread = 2}));
}

bool vv_menu_item(vv_Ctx *ctx, const char *key, const char *label,
                  const char *shortcut) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->accent};
  uint32_t id = vv_box_keyed(ctx, key, strlen(key),
                             VV_LAYOUT(.dir = VV_ROW,
                                             .w = vv_grow(1),
                                             .h = vv_fixed(28),
                                             .cross = VV_ALIGN_CENTER,
                                             .main = VV_ALIGN_SPACE_BETWEEN,
                                             .padding = vv_hv(10, 0),
                                             .focusable = true),
                             VV_STYLE(.radius = vv_r(5), .hover = &hover));
  bool hot = vv_hovered(ctx, id);
  vv_text(ctx, label,
          VV_STYLE(.fg = hot ? t->on_accent : t->text,
                     .font_size = t->font_size,
                     .font = t->font));
  if (shortcut && shortcut[0])
    vv_text(ctx, shortcut,
            VV_STYLE(.fg = hot ? t->on_accent : t->text_muted,
                       .font_size = t->font_size - 2,
                       .font = t->font));
  vv_end_box(ctx);
  bool clicked = vv_clicked(ctx, id) || vv_activated(ctx, id);
  if (clicked) {
    g_open_menu = 0;
    if (g_ctxmenu_open)
      *g_ctxmenu_open = false;
  }
  return clicked;
}

void vv_menu_separator(vv_Ctx *ctx) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(ctx, "__sep", 5,
               VV_LAYOUT(
                   .w = vv_grow(1), .h = vv_fixed(1), .padding = vv_hv(4, 0)),
               VV_STYLE(.bg = t->border));
  vv_end_box(ctx);
}

void vv_menu_end(vv_Ctx *ctx) { vv_end_box(ctx); }

// Shared panel: a floating box anchored at `at`. Callers build the dismiss
// scrim first (so it sits below the panel) with their own close behavior.
static void popover_panel(vv_Ctx *ctx, const char *key, vv_Vec2 at,
                          float width) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(ctx, key, strlen(key),
               VV_LAYOUT(.dir = VV_COLUMN,
                               .w = vv_fixed(width),
                               .padding = vv_all(14),
                               .gap = 10,
                               .has_absolute = true,
                               .z = VV_Z_POPOVER,
                               .absolute = vv_rect(at.x, at.y, width, 0)),
               VV_STYLE(.bg = t->surface_hi,
                          .radius = vv_r(10),
                          .border_width = vv_all(1),
                          .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                                     .offset = vv_v2(0, 8),
                                     .blur = 22,
                                     .spread = 2}));
}
static uint32_t popover_scrim(vv_Ctx *ctx) {
  return vv_box_keyed(
      ctx, "__povscrim", 10,
      VV_LAYOUT(.has_absolute = true,
                      .z = VV_Z_POPOVER,
                      .absolute = vv_rect(0, 0, ctx->win_w, ctx->win_h)),
      VV_STYLE(.bg = {0}));
}
static bool escape_pressed(vv_Ctx *ctx) {
  for (int i = 0; i < ctx->input.key_count; i++)
    if (ctx->input.keys[i].key == VV_KEY_ESCAPE)
      return true;
  return false;
}

void vv_popover_begin(vv_Ctx *ctx, const char *key, vv_Vec2 at, float width,
                      vv_Msg close) {
  uint32_t scrim = popover_scrim(ctx);
  if (vv_pressed(ctx, scrim) || escape_pressed(ctx))
    vv_emit(ctx, close, VV_NO_PAYLOAD);
  vv_end_box(ctx);
  popover_panel(ctx, key, at, width);
}
void vv_popover_open(vv_Ctx *ctx, const char *key, vv_Vec2 at, float width,
                     bool *open) {
  uint32_t scrim = popover_scrim(ctx);
  if (vv_pressed(ctx, scrim) || escape_pressed(ctx))
    *open = false;
  vv_end_box(ctx);
  popover_panel(ctx, key, at, width);
}
void vv_popover_end(vv_Ctx *ctx) { vv_end_box(ctx); }

typedef struct {
  float t;
} TooltipState;

void vv_tooltip(vv_Ctx *ctx, uint32_t target_id, const char *text) {
  const vv_Theme *t = vv_theme();
  TooltipState *s = vv_state(ctx, target_id, TooltipState);
  bool hot = vv_hovered(ctx, target_id);
  if (!hot) {
    s->t = 0;
    return;
  }
  s->t += ctx->dt;
  if (s->t < 0.45f) {
    vv_invalidate(ctx);
    return;
  } // keep frames coming to time it

  vv_Rect r = vv_node(ctx, target_id)->actual_rect;
  float tx = r.x, ty = r.y + r.h + 6.0f;
  vv_box_keyed(ctx, vv_fmt(ctx, "__tip%u", target_id), 0,
               VV_LAYOUT(.padding = vv_hv(9, 5),
                               .has_absolute = true,
                               .z = VV_Z_TOOLTIP,
                               .absolute = vv_rect(tx, ty, 0, 0)),
               VV_STYLE(.bg = vv_rgb(0.06f, 0.07f, 0.09f),
                          .radius = vv_r(6),
                          .border_width = vv_all(1),
                          .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.3f),
                                     .offset = vv_v2(0, 3),
                                     .blur = 10}));
  vv_text(ctx, text,
          VV_STYLE(
              .fg = t->text, .font_size = t->font_size - 1, .font = t->font));
  vv_end_box(ctx);
}

// ---- date field -----------------------------------------------------------
// A self-contained calendar picker, and the test case for "does one update
// handler scale". Every internal interaction — open/close, prev/next month,
// hover — lives in the field node's vv_state and emits NOTHING to the app; only
// picking a day emits `change` (the new packed date). So however complex the
// widget is inside, the app sees exactly one message per instance.

static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int date_dow(int y, int m, int d) { // 0 = Sunday (Sakamoto's algorithm)
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3)
    y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
static int date_dim(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
    return 29;
  return d[m - 1];
}

typedef struct {
  bool open, init;
  int vy, vm;
} DateState;

// A day square in the calendar. Pad cells (before day 1 / after month end) are
// built by the caller, keyed by grid slot.
static bool day_cell(vv_Ctx *ctx, int day, bool selected) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface};
  uint32_t id =
      vv_box_keyed(ctx, vv_fmt(ctx, "d%d", day), 0,
                   VV_LAYOUT(.w = vv_fixed(32),
                                   .h = vv_fixed(28),
                                   .main = VV_ALIGN_CENTER,
                                   .cross = VV_ALIGN_CENTER,
                                   .focusable = true),
                   VV_STYLE(.bg = selected ? t->accent : (vv_Color){0},
                              .radius = vv_r(6),
                              .hover = selected ? NULL : &hover));
  vv_text(ctx, vv_fmt(ctx, "%d", day),
          VV_STYLE(.fg = selected ? t->on_accent : t->text,
                     .font_size = t->font_size - 1));
  vv_end_box(ctx);
  return vv_clicked(ctx, id);
}

static bool nav_button(vv_Ctx *ctx, const char *key, const char *glyph) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface};
  uint32_t id = vv_box_keyed(ctx, key, strlen(key),
                             VV_LAYOUT(.w = vv_fixed(26),
                                             .h = vv_fixed(24),
                                             .main = VV_ALIGN_CENTER,
                                             .cross = VV_ALIGN_CENTER,
                                             .focusable = true),
                             VV_STYLE(.radius = vv_r(6), .hover = &hover));
  vv_text(ctx, glyph, VV_STYLE(.fg = t->text, .font_size = t->font_size));
  vv_end_box(ctx);
  return vv_clicked(ctx, id);
}

uint32_t vv_date_field(vv_Ctx *ctx, const char *key, int32_t date,
                       vv_Msg change) {
  const vv_Theme *t = vv_theme();
  int y = date / 10000, m = (date / 100) % 100, d = date % 100;
  if (m < 1 || m > 12)
    m = 1;
  if (d < 1)
    d = 1;

  vv_Style hover = {.bg = t->surface_hi};
  vv_Style focus = {.border_color = t->accent};
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.dir = VV_ROW,
                                             .w = vv_grow(1),
                                             .h = vv_fixed(34),
                                             .cross = VV_ALIGN_CENTER,
                                             .main = VV_ALIGN_SPACE_BETWEEN,
                                             .padding = vv_hv(12, 0),
                                             .focusable = true),
                             VV_STYLE(.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .hover = &hover,
                                        .focus = &focus));
  DateState *s = vv_state(ctx, id, DateState);
  if (!s->init) {
    s->vy = y;
    s->vm = m;
    s->init = true;
  }
  vv_text(
      ctx, vv_fmt(ctx, "%s %d, %d", MONTHS[m - 1], d, y),
      VV_STYLE(.fg = t->text, .font_size = t->font_size, .font = t->font));
  vv_text(ctx, "v",
          VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2));
  vv_end_box(ctx);
  // Open toggles; opening resyncs the view to the selected month.
  if (vv_clicked(ctx, id) || vv_activated(ctx, id)) {
    s->open = !s->open;
    s->vy = y;
    s->vm = m;
  }

  if (s->open) {
    const float cellw = 32, pad = 10, W = 7 * cellw + pad * 2;
    vv_Rect fr = vv_node(ctx, id)->actual_rect;

    uint32_t scrim = popover_scrim(ctx);
    if (vv_pressed(ctx, scrim) || escape_pressed(ctx))
      s->open = false;
    vv_end_box(ctx);

    vv_box_keyed(ctx, vv_fmt(ctx, "%s__cal", key ? key : "d"), 0,
                 VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(W),
                           .padding = vv_all(pad), .gap = 4,
                           .has_absolute = true, .z = VV_Z_POPOVER,
                           .absolute = vv_rect(fr.x, fr.y + fr.h + 4, W, 0)),
                 VV_STYLE(.bg = t->surface_hi, .radius = vv_r(10),
                          .border_width = vv_all(1), .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                                     .offset = vv_v2(0, 8),
                                     .blur = 22,
                                     .spread = 2}));
    // Header: < Month YYYY > — mutates the view month in place, no app message.
    VV_BOX(ctx,
           VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                     .main = VV_ALIGN_SPACE_BETWEEN),
           VV_STYLE(.bg = {0})) {
      if (nav_button(ctx, "<", "<")) {
        if (--s->vm < 1) {
          s->vm = 12;
          s->vy--;
        }
      }
      vv_text(ctx, vv_fmt(ctx, "%s %d", MONTHS[s->vm - 1], s->vy),
              VV_STYLE(.fg = t->text, .font_size = t->font_size));
      if (nav_button(ctx, ">", ">")) {
        if (++s->vm > 12) {
          s->vm = 1;
          s->vy++;
        }
      }
    }
    // Weekday header.
    VV_BOX(ctx, VV_LAYOUT(.dir = VV_ROW), VV_STYLE(.bg = {0})) {
      static const char *const WD[] = {"Su", "Mo", "Tu", "We",
                                       "Th", "Fr", "Sa"};
      for (int i = 0; i < 7; i++) {
        VV_BOX(ctx, VV_LAYOUT(.w = vv_fixed(cellw), .main = VV_ALIGN_CENTER),
               VV_STYLE(.bg = {0})) {
          vv_text(ctx, WD[i],
                  VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 3));
        }
      }
    }
    // Six week rows.
    int first = date_dow(s->vy, s->vm, 1), dim = date_dim(s->vy, s->vm);
    int day = 1 - first;
    for (int w = 0; w < 6; w++) {
      VV_BOX(ctx, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1)),
             VV_STYLE(.bg = {0})) {
        for (int wd = 0; wd < 7; wd++, day++) {
          if (day < 1 || day > dim) { // pad cell, keyed by grid slot
            vv_box_keyed(
                ctx, vv_fmt(ctx, "p%d", w * 7 + wd), 0,
                VV_LAYOUT(.w = vv_fixed(cellw), .h = vv_fixed(28)),
                VV_STYLE(.bg = {0}));
            vv_end_box(ctx);
            continue;
          }
          bool sel = day == d && s->vm == m && s->vy == y;
          if (day_cell(ctx, day, sel)) {
            vv_emit(ctx, change, vv_pi(vv_date_pack(s->vy, s->vm, day)));
            s->open = false;
          }
        }
      }
    }
    vv_end_box(ctx); // calendar panel
  }
  return id;
}

// ---- radio / progress / stepper / tabs / combobox / tree / context menu ----

uint32_t vv_radio(vv_Ctx *ctx, const char *key, const char *label,
                  bool selected, vv_Msg change, vv_Payload arg) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  vv_Style focus = {.border_color = t->accent, .border_width = vv_all(2)};
  uint32_t id = vv_box_keyed(
      ctx, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW,
                      .h = vv_fixed(30),
                      .cross = VV_ALIGN_CENTER,
                      .gap = 8,
                      .padding = vv_hv(6, 0),
                      .focusable = true,
                      .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.radius = vv_r(6), .hover = &hover, .focus = &focus));
  // Outer ring.
  vv_box_keyed(ctx, "o", 1,
               VV_LAYOUT(.w = vv_fixed(18),
                               .h = vv_fixed(18),
                               .main = VV_ALIGN_CENTER,
                               .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = t->surface,
                          .radius = vv_r(9),
                          .border_width = vv_all(2),
                          .border_color = selected ? t->accent : t->border));
  // Inner dot (grows in when selected — FLIP from 0).
  vv_box_keyed(ctx, "d", 1,
               VV_LAYOUT(.w = vv_fixed(selected ? 8 : 0),
                               .h = vv_fixed(selected ? 8 : 0)),
               VV_STYLE(.bg = t->accent, .radius = vv_r(4)));
  vv_end_box(ctx);
  vv_end_box(ctx);
  vv_text(
      ctx, label,
      VV_STYLE(.fg = t->text, .font_size = t->font_size, .font = t->font));
  vv_end_box(ctx);
  if (vv_clicked(ctx, id) || vv_activated(ctx, id))
    vv_emit(ctx, change, arg);
  return id;
}

void vv_progress(vv_Ctx *ctx, const char *key, float value) {
  const vv_Theme *t = vv_theme();
  if (value < 0)
    value = 0;
  if (value > 1)
    value = 1;
  uint32_t track =
      vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                   VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(8)),
                   VV_STYLE(.bg = t->track, .radius = vv_r(4)));
  float w = vv_node(ctx, track)->actual_rect.w;
  vv_box_keyed(ctx, "fill", 4,
               VV_LAYOUT(.has_absolute = true,
                               .absolute = vv_rect(0, 0, w * value, 8)),
               VV_STYLE(.bg = t->accent, .radius = vv_r(4)));
  vv_end_box(ctx);
  vv_end_box(ctx);
}

static bool step_btn(vv_Ctx *ctx, const char *key, const char *glyph,
                     bool enabled) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  uint32_t id = vv_box_keyed(ctx, key, strlen(key),
                             VV_LAYOUT(.w = vv_fixed(30),
                                             .h = vv_fixed(30),
                                             .main = VV_ALIGN_CENTER,
                                             .cross = VV_ALIGN_CENTER,
                                             .focusable = enabled,
                                             .cursor = VV_CURSOR_POINTER),
                             VV_STYLE(.bg = t->surface,
                                        .radius = vv_r(6),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .hover = enabled ? &hover : NULL));
  vv_text(ctx, glyph,
          VV_STYLE(.fg = enabled ? t->text : t->text_muted,
                     .font_size = t->font_size + 2));
  vv_end_box(ctx);
  return enabled && vv_clicked(ctx, id);
}

uint32_t vv_stepper(vv_Ctx *ctx, const char *key, double value, double step,
                    double min, double max, const char *unit, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t id = vv_box_keyed(
      ctx, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW, .cross = VV_ALIGN_CENTER, .gap = 8),
      VV_STYLE(.bg = {0}));
  if (step_btn(ctx, "dec", "-", value > min)) {
    double v = value - step;
    if (v < min)
      v = min;
    vv_emit(ctx, change, vv_pf(v));
  }
  vv_box_keyed(ctx, "val", 3,
               VV_LAYOUT(.w = vv_fixed(72),
                               .h = vv_fixed(30),
                               .main = VV_ALIGN_CENTER,
                               .cross = VV_ALIGN_CENTER),
               VV_STYLE(.bg = t->surface,
                          .radius = vv_r(6),
                          .border_width = vv_all(1),
                          .border_color = t->border));
  vv_text(ctx,
          unit ? vv_fmt(ctx, "%g %s", value, unit) : vv_fmt(ctx, "%g", value),
          VV_STYLE(.fg = t->text, .font_size = t->font_size));
  vv_end_box(ctx);
  if (step_btn(ctx, "inc", "+", value < max)) {
    double v = value + step;
    if (v > max)
      v = max;
    vv_emit(ctx, change, vv_pf(v));
  }
  vv_end_box(ctx);
  return id;
}

uint32_t vv_tabs(vv_Ctx *ctx, const char *key, const char *const *labels,
                 int count, int current, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  // The bar grows to fill; tabs share it equally. (The tab width can't depend
  // on the bar width AND the bar width on the tabs — grow breaks that cycle.)
  uint32_t bar = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                              VV_LAYOUT(.dir = VV_ROW,
                                              .w = vv_grow(1),
                                              .h = vv_fixed(36),
                                              .padding = vv_all(3)),
                              VV_STYLE(.bg = t->surface, .radius = vv_r(8)));
  // Sliding indicator (behind labels): FLIP-slides to the active tab's slot.
  float bw = vv_node(ctx, bar)->actual_rect.w;
  float tabw = count > 0 ? (bw - 6) / (float)count : 0;
  if (current < 0)
    current = 0;
  vv_box_keyed(ctx, "ind", 3,
               VV_LAYOUT(
                   .has_absolute = true,
                   .absolute = vv_rect(3 + tabw * (float)current, 3, tabw, 30)),
               VV_STYLE(.bg = t->accent, .radius = vv_r(6)));
  vv_end_box(ctx);
  for (int i = 0; i < count; i++) {
    bool act = i == current;
    uint32_t tid = vv_box_keyed(ctx, labels[i], strlen(labels[i]),
                                VV_LAYOUT(.w = vv_grow(1),
                                                .h = vv_fixed(30),
                                                .main = VV_ALIGN_CENTER,
                                                .cross = VV_ALIGN_CENTER,
                                                .focusable = true,
                                                .cursor = VV_CURSOR_POINTER),
                                VV_STYLE(.bg = {0}));
    vv_text(ctx, labels[i],
            VV_STYLE(.fg = act ? t->on_accent : t->text_muted,
                       .font_size = t->font_size));
    vv_end_box(ctx);
    if (vv_clicked(ctx, tid) || vv_activated(ctx, tid))
      vv_emit(ctx, change, vv_pi(i));
  }
  vv_end_box(ctx);
  return bar;
}

typedef struct {
  bool open;
} ComboState;

uint32_t vv_combobox(vv_Ctx *ctx, const char *key, const char *const *options,
                     int count, int current, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  vv_Style focus = {.border_color = t->accent};
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.dir = VV_ROW,
                                             .w = vv_grow(1),
                                             .h = vv_fixed(34),
                                             .cross = VV_ALIGN_CENTER,
                                             .main = VV_ALIGN_SPACE_BETWEEN,
                                             .padding = vv_hv(12, 0),
                                             .focusable = true,
                                             .cursor = VV_CURSOR_POINTER),
                             VV_STYLE(.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .hover = &hover,
                                        .focus = &focus));
  ComboState *s = vv_state(ctx, id, ComboState);
  const char *cur = (current >= 0 && current < count) ? options[current] : "";
  vv_text(
      ctx, cur,
      VV_STYLE(.fg = t->text, .font_size = t->font_size, .font = t->font));
  vv_text(ctx, "v",
          VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2));
  vv_end_box(ctx);
  if (vv_clicked(ctx, id) || vv_activated(ctx, id))
    s->open = !s->open;

  if (s->open) {
    vv_Rect r = vv_node(ctx, id)->actual_rect;
    uint32_t scrim = popover_scrim(ctx);
    if (vv_pressed(ctx, scrim) || escape_pressed(ctx))
      s->open = false;
    vv_end_box(ctx);
    vv_box_keyed(
        ctx, vv_fmt(ctx, "%s__list", key ? key : "c"), 0,
        VV_LAYOUT(.dir = VV_COLUMN,
                        .w = vv_fixed(r.w),
                        .padding = vv_all(4),
                        .gap = 1,
                        .has_absolute = true,
                        .z = VV_Z_POPOVER,
                        .absolute = vv_rect(r.x, r.y + r.h + 4, r.w, 0)),
        VV_STYLE(.bg = t->surface_hi,
                   .radius = vv_r(8),
                   .border_width = vv_all(1),
                   .border_color = t->border,
                   .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                              .offset = vv_v2(0, 6),
                              .blur = 18}));
    for (int i = 0; i < count; i++) {
      vv_Style ih = {.bg = t->accent};
      uint32_t oid = vv_box_keyed(
          ctx, options[i], strlen(options[i]),
          VV_LAYOUT(.dir = VV_ROW,
                          .w = vv_grow(1),
                          .h = vv_fixed(28),
                          .cross = VV_ALIGN_CENTER,
                          .padding = vv_hv(10, 0),
                          .focusable = true,
                          .cursor = VV_CURSOR_POINTER),
          VV_STYLE(.bg = i == current ? t->accent_lo : (vv_Color){0},
                     .radius = vv_r(5),
                     .hover = &ih));
      bool hot = vv_hovered(ctx, oid);
      vv_text(ctx, options[i],
              VV_STYLE(.fg = (hot || i == current) ? t->on_accent : t->text,
                         .font_size = t->font_size));
      vv_end_box(ctx);
      if (vv_clicked(ctx, oid)) {
        vv_emit(ctx, change, vv_pi(i));
        s->open = false;
      }
    }
    vv_end_box(ctx);
  }
  return id;
}

bool vv_tree_item(vv_Ctx *ctx, const char *key, const char *label, int depth,
                  bool leaf, bool expanded, bool selected) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  uint32_t id = vv_box_keyed(
      ctx, key, key ? strlen(key) : 0,
      VV_LAYOUT(.dir = VV_ROW,
                      .w = vv_grow(1),
                      .h = vv_fixed(26),
                      .cross = VV_ALIGN_CENTER,
                      .padding =
                          (vv_Edges){8.0f + (float)depth * 16.0f, 0, 8, 0},
                      .gap = 6,
                      .focusable = true,
                      .cursor = VV_CURSOR_POINTER),
      VV_STYLE(.bg = selected ? t->accent_lo : (vv_Color){0},
                 .radius = vv_r(5),
                 .hover = selected ? NULL : &hover));
  // Disclosure caret (or a spacer for leaves) keeps labels aligned.
  vv_box_keyed(ctx, "c", 1, VV_LAYOUT(.w = vv_fixed(12)),
               VV_STYLE(.bg = {0}));
  if (!leaf)
    vv_text(ctx, expanded ? "v" : ">",
            VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 3));
  vv_end_box(ctx);
  vv_text(ctx, label,
          VV_STYLE(.fg = selected ? t->on_accent : t->text,
                     .font_size = t->font_size));
  vv_end_box(ctx);
  return vv_clicked(ctx, id);
}

void vv_context_menu_begin(vv_Ctx *ctx, const char *key, vv_Vec2 at,
                           bool *open) {
  const vv_Theme *t = vv_theme();
  g_ctxmenu_open = open;
  uint32_t scrim = popover_scrim(ctx);
  if (vv_pressed(ctx, scrim) || escape_pressed(ctx))
    *open = false;
  vv_end_box(ctx);
  vv_box_keyed(ctx, key, strlen(key),
               VV_LAYOUT(.dir = VV_COLUMN,
                               .w = vv_fixed(200),
                               .padding = vv_all(5),
                               .gap = 1,
                               .has_absolute = true,
                               .z = VV_Z_POPOVER,
                               .absolute = vv_rect(at.x, at.y, 200, 0)),
               VV_STYLE(.bg = t->surface_hi,
                          .radius = vv_r(8),
                          .border_width = vv_all(1),
                          .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                                     .offset = vv_v2(0, 6),
                                     .blur = 18,
                                     .spread = 2}));
}
void vv_context_menu_end(vv_Ctx *ctx) {
  vv_end_box(ctx);
  g_ctxmenu_open = NULL;
}

// ==========================================================================
// Visualizer widgets (§14.5). These lean on the vv_draw_* canvas (vv_draw.h)
// for vector content and otherwise follow the slider pattern: read the node's
// actual_rect, map the pointer while active, emit the new value.
// ==========================================================================

// ---- xy_pad ---------------------------------------------------------------
uint32_t vv_xy_pad(vv_Ctx *ctx, const char *key, vv_Vec2 value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.w = {VV_SIZE_GROW, 1, 140, 0},
                                             .h = vv_fixed(160),
                                             .focusable = true, .clip = true),
                             VV_STYLE(.bg = t->surface, .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border));
  vv_Node *n = vv_node(ctx, id);
  float w = n->actual_rect.w > 1.0f ? n->actual_rect.w : 140.0f;
  float h = n->actual_rect.h > 1.0f ? n->actual_rect.h : 160.0f;

  // Inset so handles at the 0/1 edges aren't clipped by the box.
  const float M = 10.0f;
  float iw = vv_maxf(w - 2 * M, 1.0f), ih = vv_maxf(h - 2 * M, 1.0f);

  vv_Vec2 out = value;
  if (vv_pressed(ctx, id)) vv_focus(ctx, id);
  if (vv_active(ctx, id)) {
    float rx = vv_clampf((vv_mouse(ctx).x - n->actual_rect.x - M) / iw, 0.0f, 1.0f);
    float ry = vv_clampf((vv_mouse(ctx).y - n->actual_rect.y - M) / ih, 0.0f, 1.0f);
    out = vv_v2(rx, 1.0f - ry); // y is up
  }

  // Center gridlines, then the live crosshair through the handle.
  vv_Color grid = vv_rgba(t->border.r, t->border.g, t->border.b, 0.6f);
  vv_draw_line(ctx, id, vv_v2(0, h * 0.5f), vv_v2(w, h * 0.5f), 1.0f, grid);
  vv_draw_line(ctx, id, vv_v2(w * 0.5f, 0), vv_v2(w * 0.5f, h), 1.0f, grid);
  float hx = M + out.x * iw, hy = M + (1.0f - out.y) * ih;
  vv_draw_line(ctx, id, vv_v2(0, hy), vv_v2(w, hy), 1.0f, t->accent_lo);
  vv_draw_line(ctx, id, vv_v2(hx, 0), vv_v2(hx, h), 1.0f, t->accent_lo);

  // Handle (absolute child so it FLIP-springs when the value jumps).
  vv_Style hover = {.bg = t->accent_hi};
  vv_box_keyed(ctx, "h", 1,
               VV_LAYOUT(.w = vv_fixed(16), .h = vv_fixed(16), .has_absolute = true,
                               .absolute = vv_rect(hx - 8, hy - 8, 16, 16)),
               VV_STYLE(.bg = t->accent, .radius = vv_r(8),
                          .border_width = vv_all(2), .border_color = t->on_accent,
                          .hover = &hover));
  vv_end_box(ctx);
  vv_end_box(ctx);

  if (out.x != value.x || out.y != value.y) vv_emit(ctx, change, vv_pv2(out));
  return id;
}

// ---- plot -----------------------------------------------------------------
void vv_plot(vv_Ctx *ctx, const char *key, const vv_PlotSeries *series, int n,
             vv_PlotOpts o) {
  const vv_Theme *t = vv_theme();
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.w = vv_grow(1),
                                             .h = o.height > 0 ? vv_fixed(o.height) : vv_grow(1),
                                             .clip = true),
                             VV_STYLE(.bg = t->surface, .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border));
  vv_Node *nd = vv_node(ctx, id);
  float w = nd->actual_rect.w, h = nd->actual_rect.h;
  if (w < 2.0f || h < 2.0f) { vv_end_box(ctx); return; } // not laid out yet

  const float PAD = 10.0f;
  float ix = PAD, iy = PAD, iw = w - 2 * PAD, ih = h - 2 * PAD;
  if (iw < 1.0f || ih < 1.0f) { vv_end_box(ctx); return; }

  // Ranges: explicit, or fit to the data.
  float xmin = o.x_min, xmax = o.x_max, ymin = o.y_min, ymax = o.y_max;
  if (o.auto_x || xmin == xmax) { xmin = 1e30f; xmax = -1e30f; }
  if (o.auto_y || ymin == ymax) { ymin = 1e30f; ymax = -1e30f; }
  if (o.auto_x || o.auto_y || o.x_min == o.x_max || o.y_min == o.y_max) {
    for (int s = 0; s < n; s++) {
      const vv_PlotSeries *ps = &series[s];
      for (int k = 0; k < ps->count; k++) {
        float x = ps->xs ? ps->xs[k] : (float)k;
        float y = ps->ys[k];
        if (o.auto_x || o.x_min == o.x_max) { xmin = vv_minf(xmin, x); xmax = vv_maxf(xmax, x); }
        if (o.auto_y || o.y_min == o.y_max) { ymin = vv_minf(ymin, y); ymax = vv_maxf(ymax, y); }
      }
    }
  }
  if (xmax <= xmin) { xmin -= 0.5f; xmax += 0.5f; }
  if (ymax <= ymin) { ymin -= 0.5f; ymax += 0.5f; }

  // Gridlines (5x4), drawn under the data.
  if (o.grid) {
    vv_Color g = vv_rgba(t->border.r, t->border.g, t->border.b, 0.5f);
    for (int i = 0; i <= 4; i++) {
      float gx = ix + iw * (float)i / 4.0f;
      vv_draw_line(ctx, id, vv_v2(gx, iy), vv_v2(gx, iy + ih), 1.0f, g);
    }
    for (int i = 0; i <= 4; i++) {
      float gy = iy + ih * (float)i / 4.0f;
      vv_draw_line(ctx, id, vv_v2(ix, gy), vv_v2(ix + iw, gy), 1.0f, g);
    }
  }

  // Each series, mapped data-space -> local pixels (y flipped: ymax at top).
  vv_Vec2 buf[1024];
  float base = iy + vv_remapf(vv_clampf(0.0f, ymin, ymax), ymax, ymin, 0.0f, ih);
  for (int s = 0; s < n; s++) {
    const vv_PlotSeries *ps = &series[s];
    if (ps->count < 1) continue;
    int step = ps->count > 1024 ? (ps->count + 1023) / 1024 : 1; // decimate
    int m = 0;
    for (int k = 0; k < ps->count && m < 1024; k += step) {
      float x = ps->xs ? ps->xs[k] : (float)k;
      buf[m++] = vv_v2(ix + vv_remapf(x, xmin, xmax, 0.0f, iw),
                       iy + vv_remapf(ps->ys[k], ymax, ymin, 0.0f, ih));
    }
    float lw = ps->width > 0 ? ps->width : 2.0f;
    switch (ps->kind) {
      case VV_PLOT_SCATTER:
        vv_draw_points(ctx, id, buf, m, lw > 0 ? lw : 3.0f, ps->color);
        break;
      case VV_PLOT_BARS: {
        float bw = iw / (float)(m > 0 ? m : 1) * 0.6f;
        if (bw < 1.0f) bw = 1.0f;
        for (int k = 0; k < m; k++) {
          vv_Vec2 q[4] = {{buf[k].x - bw / 2, buf[k].y}, {buf[k].x + bw / 2, buf[k].y},
                          {buf[k].x + bw / 2, base}, {buf[k].x - bw / 2, base}};
          vv_draw_polygon(ctx, id, q, 4, ps->color);
        }
      } break;
      case VV_PLOT_LINE:
      default:
        vv_draw_polyline(ctx, id, buf, m, lw, ps->color);
        break;
    }
  }

  // Corner y-range labels.
  vv_box_keyed(ctx, "yl", 2,
               VV_LAYOUT(.has_absolute = true, .absolute = vv_rect(ix + 2, iy - 2, 60, 16)),
               VV_STYLE(.bg = {0}));
  vv_text(ctx, vv_fmt(ctx, "%.2f", (double)ymax),
          VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 4));
  vv_end_box(ctx);
  vv_box_keyed(ctx, "yl0", 3,
               VV_LAYOUT(.has_absolute = true, .absolute = vv_rect(ix + 2, iy + ih - 16, 60, 16)),
               VV_STYLE(.bg = {0}));
  vv_text(ctx, vv_fmt(ctx, "%.2f", (double)ymin),
          VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 4));
  vv_end_box(ctx);

  vv_end_box(ctx);
}

// ---- curve_editor ---------------------------------------------------------
uint32_t vv_curve_editor(vv_Ctx *ctx, const char *key, const vv_Vec2 *pts,
                         int count, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t id = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                             VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(180), .clip = true),
                             VV_STYLE(.bg = t->surface, .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border));
  vv_Node *nd = vv_node(ctx, id);
  float w = nd->actual_rect.w > 1.0f ? nd->actual_rect.w : 200.0f;
  float h = nd->actual_rect.h > 1.0f ? nd->actual_rect.h : 180.0f;
  const float M = 10.0f; // inset so edge handles aren't clipped
  float iw = vv_maxf(w - 2 * M, 1.0f), ih = vv_maxf(h - 2 * M, 1.0f);

  // Gridlines under the curve.
  vv_Color g = vv_rgba(t->border.r, t->border.g, t->border.b, 0.5f);
  for (int i = 1; i < 4; i++) {
    vv_draw_line(ctx, id, vv_v2(w * (float)i / 4, 0), vv_v2(w * (float)i / 4, h), 1.0f, g);
    vv_draw_line(ctx, id, vv_v2(0, h * (float)i / 4), vv_v2(w, h * (float)i / 4), 1.0f, g);
  }

  // The polyline through the (normalized, y-up) control points.
  int m = count > 128 ? 128 : count;
  vv_Vec2 buf[128];
  for (int k = 0; k < m; k++)
    buf[k] = vv_v2(M + vv_clampf(pts[k].x, 0, 1) * iw,
                   M + (1.0f - vv_clampf(pts[k].y, 0, 1)) * ih);
  if (m >= 2) vv_draw_polyline(ctx, id, buf, m, 2.0f, t->accent);

  // Draggable control points (each its own interactive child).
  vv_Style hover = {.bg = t->accent_hi};
  for (int k = 0; k < m; k++) {
    uint32_t pid = vv_box_keyed(ctx, vv_fmt(ctx, "p%d", k), 0,
                                VV_LAYOUT(.w = vv_fixed(14), .h = vv_fixed(14),
                                                .has_absolute = true, .focusable = true,
                                                .absolute = vv_rect(buf[k].x - 7, buf[k].y - 7, 14, 14)),
                                VV_STYLE(.bg = t->knob, .radius = vv_r(7),
                                           .border_width = vv_all(2),
                                           .border_color = t->accent, .hover = &hover));
    vv_end_box(ctx);
    if (vv_pressed(ctx, pid)) vv_focus(ctx, pid);
    if (vv_active(ctx, pid)) {
      float nx = vv_clampf((vv_mouse(ctx).x - nd->actual_rect.x - M) / iw, 0.0f, 1.0f);
      float ny = vv_clampf((vv_mouse(ctx).y - nd->actual_rect.y - M) / ih, 0.0f, 1.0f);
      vv_CurveEdit *e = vv_arena_alloc(&ctx->frame, sizeof *e);
      e->index = k;
      e->pos = vv_v2(nx, 1.0f - ny);
      vv_emit(ctx, change, vv_pp(e));
    }
  }

  vv_end_box(ctx);
  return id;
}
