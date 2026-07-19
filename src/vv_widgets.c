#include "verve/vv_widgets.h"
#include "verve/vv_layout.h"
#include "verve/vv_value.h"

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
  if (on.hover && vv_hovered(ctx, id)) vv_emit(ctx, on.hover, VV_NO_PAYLOAD);
  if (on.press && vv_pressed(ctx, id)) vv_emit(ctx, on.press, VV_NO_PAYLOAD);
  if (on.dbl && vv_double_clicked(ctx, id)) vv_emit(ctx, on.dbl, VV_NO_PAYLOAD);
  vv_node(ctx, id)->on_move = on.move;
}

// ---- label ---------------------------------------------------------------

void vv_label(vv_Ctx *ctx, const char *text) {
  const vv_Theme *t = vv_theme();
  vv_text(
      ctx, text,
      (vv_Style){.fg = t->text, .font_size = t->font_size, .font = t->font});
}
void vv_label_muted(vv_Ctx *ctx, const char *text) {
  const vv_Theme *t = vv_theme();
  vv_text(ctx, text,
          (vv_Style){
              .fg = t->text_muted, .font_size = t->font_size, .font = t->font});
}

// ---- button --------------------------------------------------------------

uint32_t vv_button_on(vv_Ctx *ctx, const char *key, const char *label,
                      vv_Msg click, vv_Payload arg, vv_On on) {
  const vv_Theme *t = vv_theme();
  // Variants are consumed at build time (§7.1), so these locals are fine.
  vv_Style hover = {.bg = t->accent_hi};
  vv_Style active = {.bg = t->accent_lo, .transform = vv_scale(0.97f)};
  uint32_t id =
      vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                   VV_LAYOUT(.w = vv_fit(), .h = vv_fixed(38),
                             .padding = vv_hv(16, 9), .main = VV_ALIGN_CENTER,
                             .cross = VV_ALIGN_CENTER, .focusable = true),
                   VV_STYLE(.bg = t->accent, .radius = vv_r(t->radius),
                            .hover = &hover, .active = &active));

  vv_text(ctx, label,
          (vv_Style){
              .fg = t->on_accent, .font_size = t->font_size, .font = t->font});
  vv_end_box(ctx);
  if (vv_clicked(ctx, id)) vv_emit(ctx, click, arg);
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
               VV_LAYOUT(.w = vv_fixed(20),
                               .h = vv_fixed(20),
                               .has_absolute = true,
                               .absolute = vv_rect(value ? 23 : 3, 3, 20, 20)),
               VV_STYLE(.bg = t->knob, .radius = vv_r(10)));
  vv_end_box(ctx);
  vv_end_box(ctx);
  if (vv_clicked(ctx, id)) vv_emit(ctx, change, vv_pi(!value));
  return id;
}

// ---- checkbox ------------------------------------------------------------

uint32_t vv_checkbox(vv_Ctx *ctx, const char *key, const char *label,
                     bool value, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  uint32_t row = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                              (vv_LayoutDecl){.dir = VV_ROW,
                                              .gap = 10,
                                              .cross = VV_ALIGN_CENTER,
                                              .focusable = true},
                              (vv_Style){0});
  {
    vv_box_keyed(ctx, "box", 3,
                 (vv_LayoutDecl){.w = vv_fixed(20), .h = vv_fixed(20)},
                 (vv_Style){.bg = value ? t->accent : t->surface_hi,
                            .radius = vv_r(5),
                            .border_width = vv_all(1),
                            .border_color = value ? t->accent : t->border});
    {
      if (value) {
        // A simple checkmark drawn as a small rotated bar pair via a box.
        vv_box_keyed(ctx, "tick", 9,
                     (vv_LayoutDecl){.w = vv_fixed(10),
                                     .h = vv_fixed(10),
                                     .has_absolute = true,
                                     .absolute = vv_rect(5, 5, 10, 10)},
                     (vv_Style){.bg = t->on_accent, .radius = vv_r(2)});
        vv_end_box(ctx);
      }
    }
    vv_end_box(ctx);
    vv_label(ctx, label);
  }
  vv_end_box(ctx);
  if (vv_clicked(ctx, row)) vv_emit(ctx, change, vv_pi(!value));
  return row;
}

// ---- slider --------------------------------------------------------------

typedef struct {
  bool was_active;
} SliderState;

typedef struct {
  uint32_t id;
  float    value;
  bool     changed, press, release;
} SliderResult;

// Shared slider core. Deals in value space, mapping pointer position through the
// perceptual curve (meta may be NULL => linear). Reports press/release edges so
// bound callers can bracket a transactional edit (§12.1).
static SliderResult slider_core(vv_Ctx *ctx, const char *key, float value,
                                float min, float max, const vv_ValueMeta *meta) {
  const vv_Theme *t = vv_theme();
  float norm = vv_value_norm(meta, min, max, value);

  // A grow track with a real minimum so the slider keeps usable geometry even
  // inside a FIT parent (a bare row): with min 0 it would collapse to width 0,
  // the visuals would fall back to a fixed width, and the drag math below —
  // gated on actual width — would silently never fire (§8.2).
  uint32_t track = vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                                (vv_LayoutDecl){.w = {VV_SIZE_GROW, 1, 160, 0},
                                                .h = vv_fixed(28),
                                                .focusable = true,
                                                .cross = VV_ALIGN_CENTER},
                                (vv_Style){0});

  // Interaction: while active, map pointer x within the track to a value. Uses
  // last frame's track geometry (the §4.5 lag) — invisible at speed.
  float out = value;
  vv_Node *tn = vv_node(ctx, track);
  float tw = tn->actual_rect.w > 1.0f ? tn->actual_rect.w : 160.0f;
  bool active = vv_active(ctx, track);
  if (active) {
    float rel = vv_clampf((vv_mouse(ctx).x - tn->actual_rect.x) / tw, 0.0f, 1.0f);
    out = vv_value_denorm(meta, min, max, rel);
    norm = rel;
  }

  SliderState *st = vv_state(ctx, track, SliderState);
  SliderResult r = {.id = track, .value = out, .changed = out != value,
                    .press = vv_pressed(ctx, track),
                    .release = st->was_active && !active};
  st->was_active = active;

  float hx = norm * (tw - 18.0f);
  {
    // Rail.
    vv_box_keyed(ctx, "rail", 1,
                 (vv_LayoutDecl){.w = vv_grow(1),
                                 .h = vv_fixed(6),
                                 .has_absolute = true,
                                 .absolute = vv_rect(0, 11, tw, 6)},
                 (vv_Style){.bg = t->track, .radius = vv_r(3)});
    vv_end_box(ctx);
    // Filled portion.
    vv_box_keyed(ctx, "fill", 2,
                 (vv_LayoutDecl){.has_absolute = true,
                                 .absolute = vv_rect(0, 11, hx + 9, 6)},
                 (vv_Style){.bg = t->accent, .radius = vv_r(3)});
    vv_end_box(ctx);
    // Handle (springs along x via FLIP when value jumps).
    vv_Style hhover = {.bg = t->accent_hi};
    vv_box_keyed(ctx, "handle", 3,
                 (vv_LayoutDecl){.w = vv_fixed(18),
                                 .h = vv_fixed(18),
                                 .has_absolute = true,
                                 .absolute = vv_rect(hx, 5, 18, 18)},
                 (vv_Style){.bg = t->knob,
                            .radius = vv_r(9),
                            .shadow = {.color = vv_rgba(0, 0, 0, 0.3f),
                                       .offset = vv_v2(0, 2),
                                       .blur = 6},
                            .hover = &hhover});
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
  if (r.changed) vv_emit(ctx, change, vv_pf(r.value));
  return r.id;
}

uint32_t vv_slider_bound(vv_Ctx *ctx, const char *key, vv_Value v) {
  float value = v.ptr ? *(float *)v.ptr : 0.0f;
  float min = v.meta ? v.meta->min : 0.0f;
  float max = v.meta ? v.meta->max : 1.0f;
  bool readonly = v.meta && (v.meta->flags & VV_VAL_READONLY);
  SliderResult r = slider_core(ctx, key, value, min, max, v.meta);
  if (!readonly) {
    if (r.press) vv_begin_edit(ctx, v);
    if (r.changed) emit_bind(ctx, v, vv_pf(r.value));
    if (r.release) vv_end_edit(ctx, v);
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
                             (vv_LayoutDecl){.w = vv_fixed(90),
                                             .h = vv_fixed(32),
                                             .padding = vv_hv(10, 6),
                                             .main = VV_ALIGN_CENTER,
                                             .cross = VV_ALIGN_CENTER,
                                             .focusable = true},
                             (vv_Style){.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .hover = &hover});

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
      (vv_Style){.fg = t->text, .font_size = t->font_size, .font = t->font});
  vv_end_box(ctx);
  return (SliderResult){.id = id, .value = out, .changed = out != value,
                        .press = press, .release = release};
}

uint32_t vv_drag_number(vv_Ctx *ctx, const char *key, float value, float speed,
                        float min, float max, vv_Msg change) {
  SliderResult r = drag_core(ctx, key, value, speed, min, max);
  if (r.changed) vv_emit(ctx, change, vv_pf(r.value));
  return r.id;
}

uint32_t vv_drag_number_bound(vv_Ctx *ctx, const char *key, vv_Value v,
                              float speed) {
  float value = v.ptr ? *(float *)v.ptr : 0.0f;
  float min = v.meta ? v.meta->min : 0.0f;
  float max = v.meta ? v.meta->max : 0.0f; // 0,0 => unclamped
  bool readonly = v.meta && (v.meta->flags & VV_VAL_READONLY);
  SliderResult r = drag_core(ctx, key, value, speed, min, max);
  if (!readonly) {
    if (r.press) vv_begin_edit(ctx, v);
    if (r.changed) emit_bind(ctx, v, vv_pf(r.value));
    if (r.release) vv_end_edit(ctx, v);
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
  if (!readonly && vv_clicked(ctx, id)) emit_bind(ctx, v, vv_pi(!value));
  return id;
}

uint32_t vv_toggle_bound(vv_Ctx *ctx, const char *key, vv_Value v) {
  bool value = v.ptr ? *(bool *)v.ptr : false;
  bool readonly = v.meta && (v.meta->flags & VV_VAL_READONLY);
  uint32_t id = vv_toggle(ctx, key, value, VV_MSG_NONE);
  if (!readonly && vv_clicked(ctx, id)) emit_bind(ctx, v, vv_pi(!value));
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

// Byte index whose caret is nearest pointer x (relative to text start).
static int char_from_x(vv_Ctx *ctx, const char *buf, int len, float size,
                       float x) {
  if (x <= 0)
    return 0;
  float prev = 0;
  for (int i = 1; i <= len; i++) {
    float w = measure_prefix(ctx, buf, i, size);
    if (x < (prev + w) * 0.5f)
      return i - 1;
    prev = w;
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
  memmove(buf + lo, buf + hi, (size_t)(*len - hi + 1)); // include NUL
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
          (size_t)(*len - s->cursor + 1));
  memcpy(buf + s->cursor, ins, (size_t)ilen);
  *len += ilen;
  s->cursor += ilen;
  s->anchor = s->cursor;
}

// Returns true if the buffer changed.
static bool handle_key(vv_Ctx *ctx, char *buf, int *len, int cap,
                       TextFieldState *s, vv_KeyEvent ev) {
  (void)ctx;
  bool changed = false;
  int prev = s->cursor;
  switch (ev.key) {
  case VV_KEY_LEFT:
    if (s->cursor > 0)
      s->cursor--;
    break;
  case VV_KEY_RIGHT:
    if (s->cursor < *len)
      s->cursor++;
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
      erase_range(buf, len, s->cursor - 1, s->cursor);
      s->cursor--;
      s->anchor = s->cursor;
      changed = true;
    }
    break;
  case VV_KEY_DELETE:
    if (delete_selection(buf, len, s)) {
      changed = true;
    } else if (s->cursor < *len) {
      erase_range(buf, len, s->cursor, s->cursor + 1);
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
    // Copy: clipboard is owned by the backend, not the core; a future
    // vv_clipboard_set(ctx, ...) will route here. No-op for now.
    break;
  case VV_KEY_X:
    if (ev.ctrl && has_sel(s)) {
      delete_selection(buf, len, s);
      changed = true;
    }
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
                             (vv_LayoutDecl){.dir = VV_ROW,
                                             .w = vv_grow(1),
                                             .h = vv_fixed(34),
                                             .padding = vv_hv(10, 0),
                                             .cross = VV_ALIGN_CENTER,
                                             .focusable = true,
                                             .clip = true},
                             (vv_Style){.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .hover = &hover,
                                        .focus = &focus});

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

  // Caret target position. The glide is the caret node's own FLIP spring, driven
  // by present every frame while unsettled — NOT a build-stepped spring, which
  // would freeze between rebuilds now that builds are gated (§4.2) and leave the
  // caret parked at a partial position.
  float cx = measure_prefix(ctx, buf, s->cursor, size);

  // Selection highlight (behind text).
  if (focused && has_sel(s)) {
    float lo = measure_prefix(ctx, buf, sel_lo(s), size);
    float hi = measure_prefix(ctx, buf, sel_hi(s), size);
    vv_box_keyed(
        ctx, "sel", 3,
        (vv_LayoutDecl){.has_absolute = true,
                        .absolute = vv_rect(lo, 6, hi - lo, 22)},
        (vv_Style){.bg = vv_rgba(t->accent.r, t->accent.g, t->accent.b, 0.35f),
                   .radius = vv_r(3)});
    vv_end_box(ctx);
  }

  // Text or placeholder.
  if (len == 0 && placeholder && !focused)
    vv_text(
        ctx, placeholder,
        (vv_Style){.fg = t->text_muted, .font_size = size, .font = t->font});
  else
    vv_text(ctx, buf,
            (vv_Style){.fg = t->text, .font_size = size, .font = t->font});

  // Caret.
  if (focused) {
    vv_box_keyed(ctx, "caret", 7,
                 (vv_LayoutDecl){.w = vv_fixed(2),
                                 .h = vv_fixed(20),
                                 .has_absolute = true,
                                 .absolute = vv_rect(cx, 7, 2, 20)},
                 (vv_Style){.bg = t->accent, .radius = vv_r(1)});
    vv_end_box(ctx);
  }

  vv_end_box(ctx);
  if (changed) vv_emit(ctx, change, vv_ps(buf));
  return id;
}

// ---- list item -----------------------------------------------------------

uint32_t vv_list_item(vv_Ctx *ctx, const char *key, const char *label,
                      bool selected, vv_Msg click, vv_Payload arg) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  uint32_t id =
      vv_box_keyed(ctx, key, key ? strlen(key) : 0,
                   (vv_LayoutDecl){.dir = VV_ROW,
                                   .w = vv_grow(1),
                                   .h = vv_fixed(34),
                                   .padding = vv_hv(12, 0),
                                   .cross = VV_ALIGN_CENTER,
                                   .focusable = true},
                   (vv_Style){.bg = selected ? t->accent_lo : t->surface,
                              .radius = vv_r(6),
                              .hover = selected ? NULL : &hover});
  vv_text(ctx, label,
          (vv_Style){.fg = selected ? t->on_accent : t->text,
                     .font_size = t->font_size,
                     .font = t->font});
  vv_end_box(ctx);
  if (vv_clicked(ctx, id)) vv_emit(ctx, click, arg);
  return id;
}

// ---- multi-line text area -------------------------------------------------
// Reuses the single-line editor's byte-offset model (TextFieldState) and its
// edit helpers, adding line-aware navigation. Lines are split on '\n'; the caret
// column is measured with the same measure_prefix the backend draws with, so the
// caret and glyphs agree. line_h mirrors the backend's size * 1.25.

static int ml_line_start(const char *buf, int pos) {
  while (pos > 0 && buf[pos - 1] != '\n') pos--;
  return pos;
}
static int ml_line_end(const char *buf, int len, int pos) {
  while (pos < len && buf[pos] != '\n') pos++;
  return pos;
}
static int ml_line_index(const char *buf, int pos) {
  int line = 0;
  for (int i = 0; i < pos; i++) if (buf[i] == '\n') line++;
  return line;
}

// Move cursor up/down one line, aiming for pixel column `goal_x`.
static void ml_vmove(vv_Ctx *ctx, char *buf, int len, TextFieldState *s,
                     float size, float goal_x, int dir) {
  int ls = ml_line_start(buf, s->cursor);
  if (dir < 0) {
    if (ls == 0) { s->cursor = 0; return; }
    int prev_start = ml_line_start(buf, ls - 1);
    int prev_end = ls - 1;
    int col = char_from_x(ctx, buf + prev_start, prev_end - prev_start, size, goal_x);
    s->cursor = prev_start + col;
  } else {
    int le = ml_line_end(buf, len, s->cursor);
    if (le >= len) { s->cursor = len; return; }
    int next_start = le + 1;
    int next_end = ml_line_end(buf, len, next_start);
    int col = char_from_x(ctx, buf + next_start, next_end - next_start, size, goal_x);
    s->cursor = next_start + col;
  }
}

typedef struct { int cursor, anchor; float goal_x; bool have_goal; } TextAreaState;

static bool ml_handle_key(vv_Ctx *ctx, char *buf, int *len, int cap,
                          TextAreaState *s, vv_KeyEvent ev, float size) {
  bool changed = false;
  TextFieldState *ts = (TextFieldState *)s; // cursor/anchor share layout
  bool vertical = (ev.key == VV_KEY_UP || ev.key == VV_KEY_DOWN);
  if (!vertical) s->have_goal = false;
  switch (ev.key) {
  case VV_KEY_LEFT:  if (ts->cursor > 0) ts->cursor--; break;
  case VV_KEY_RIGHT: if (ts->cursor < *len) ts->cursor++; break;
  case VV_KEY_HOME:  ts->cursor = ml_line_start(buf, ts->cursor); break;
  case VV_KEY_END:   ts->cursor = ml_line_end(buf, *len, ts->cursor); break;
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
    if (delete_selection(buf, len, ts)) changed = true;
    else if (ts->cursor > 0) { erase_range(buf, len, ts->cursor - 1, ts->cursor); ts->cursor--; ts->anchor = ts->cursor; changed = true; }
    break;
  case VV_KEY_DELETE:
    if (delete_selection(buf, len, ts)) changed = true;
    else if (ts->cursor < *len) { erase_range(buf, len, ts->cursor, ts->cursor + 1); changed = true; }
    break;
  case VV_KEY_A:
    if (ev.ctrl) { ts->anchor = 0; ts->cursor = *len; }
    break;
  default: break;
  }
  if (ev.shift) { /* extend: keep anchor */ }
  else if (ev.key == VV_KEY_LEFT || ev.key == VV_KEY_RIGHT ||
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
                             (vv_LayoutDecl){.dir = VV_COLUMN,
                                             .w = vv_grow(1),
                                             .h = vh,
                                             .padding = vv_all(pad),
                                             .focusable = true,
                                             .clip = true,
                                             .scroll_y = true},
                             (vv_Style){.bg = t->surface,
                                        .radius = vv_r(t->radius),
                                        .border_width = vv_all(1),
                                        .border_color = t->border,
                                        .focus = &focus});
  bool focused = vv_focused(ctx, id);
  TextAreaState *s = vv_state(ctx, id, TextAreaState);
  TextFieldState *ts = (TextFieldState *)s;
  bool changed = false;

  vv_Rect fr = vv_node(ctx, id)->actual_rect;
  float text_x0 = fr.x + pad;
  float text_y0 = fr.y + pad; // scroll offset is small; approximate hit for click
  if (vv_pressed(ctx, id)) {
    vv_focus(ctx, id);
    int line = (int)((vv_mouse(ctx).y - text_y0) / line_h);
    if (line < 0) line = 0;
    // Walk to the requested visual line, then to the column.
    int p = 0, cur = 0;
    while (cur < line && p < len) { if (buf[p] == '\n') cur++; p++; }
    int ls = p, le = ml_line_end(buf, len, p);
    ts->cursor = ts->anchor = ls + char_from_x(ctx, buf + ls, le - ls, size, vv_mouse(ctx).x - text_x0);
    s->have_goal = false;
  }

  if (focused) {
    if (ctx->input.text_len > 0) {
      insert_text(buf, &len, cap, ts, ctx->input.text, ctx->input.text_len);
      s->have_goal = false;
      changed = true;
    }
    for (int i = 0; i < ctx->input.key_count; i++)
      changed |= ml_handle_key(ctx, buf, &len, cap, s, ctx->input.keys[i], size);
  }
  if (ts->cursor > len) ts->cursor = len;
  if (ts->anchor > len) ts->anchor = len;

  // Selection: one highlight rect per spanned line (behind the text).
  if (focused && has_sel(ts)) {
    int lo = sel_lo(ts), hi = sel_hi(ts);
    int p = lo;
    while (p <= hi) {
      int ls = ml_line_start(buf, p);
      int le = ml_line_end(buf, len, p);
      int a = p > lo ? ls : lo;         // first spanned line starts at lo
      int b = le < hi ? le : hi;        // last spanned line ends at hi
      float ax = measure_prefix(ctx, buf + ls, a - ls, size);
      float bx = measure_prefix(ctx, buf + ls, b - ls, size);
      float extra = (le < hi) ? 4.0f : 0.0f; // hint the newline is included
      float ly = (float)ml_line_index(buf, ls) * line_h;
      vv_box_keyed(ctx, vv_fmt(ctx, "sel%d", ls), 0,
                   (vv_LayoutDecl){.has_absolute = true,
                                   .absolute = vv_rect(pad + ax, pad + ly, bx - ax + extra, line_h)},
                   (vv_Style){.bg = vv_rgba(t->accent.r, t->accent.g, t->accent.b, 0.30f),
                              .radius = vv_r(2)});
      vv_end_box(ctx);
      if (le >= len) break;
      p = le + 1;
    }
  }

  if (len == 0 && placeholder && !focused)
    vv_text(ctx, placeholder,
            (vv_Style){.fg = t->text_muted, .font_size = size, .font = t->font});
  else
    vv_text(ctx, buf, (vv_Style){.fg = t->text, .font_size = size, .font = t->font});

  // Caret at (column x, line y).
  if (focused) {
    int ls = ml_line_start(buf, ts->cursor);
    float cx = measure_prefix(ctx, buf + ls, ts->cursor - ls, size);
    float cy = (float)ml_line_index(buf, ts->cursor) * line_h;
    vv_box_keyed(ctx, "caret", 7,
                 (vv_LayoutDecl){.w = vv_fixed(2), .h = vv_fixed(line_h),
                                 .has_absolute = true,
                                 .absolute = vv_rect(pad + cx, pad + cy, 2, line_h)},
                 (vv_Style){.bg = t->accent, .radius = vv_r(1)});
    vv_end_box(ctx);
  }

  vv_end_box(ctx);
  if (changed) vv_emit(ctx, change, vv_ps(buf));
  return id;
}

// ---- overlay chrome: menus, popovers, tooltips ----------------------------
// Overlays paint on top only if built last in the tree (the painter is strict
// tree order). The menu system self-manages which menu is open via one static —
// menus are transient and mutually exclusive, so this stays simple.

static vv_ID g_open_menu; // node id of the open menu title, 0 = none

void vv_menubar_begin(vv_Ctx *ctx) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(ctx, "__menubar", 9,
               (vv_LayoutDecl){.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(34),
                               .cross = VV_ALIGN_CENTER, .padding = vv_hv(6, 0), .gap = 2},
               (vv_Style){.bg = t->surface,
                          .border_width = (vv_Edges){0, 0, 0, 1},
                          .border_color = t->border});
}
void vv_menubar_end(vv_Ctx *ctx) { vv_end_box(ctx); }

uint32_t vv_menu_title(vv_Ctx *ctx, const char *key, const char *label) {
  const vv_Theme *t = vv_theme();
  // Resolve open state before styling: needs this node's stable id, which we get
  // from the handle after opening. Open one frame late is invisible here.
  uint32_t id = vv_box_keyed(ctx, key, strlen(key),
                             (vv_LayoutDecl){.dir = VV_ROW, .h = vv_fixed(24),
                                             .cross = VV_ALIGN_CENTER,
                                             .padding = vv_hv(10, 0), .focusable = true},
                             (vv_Style){.radius = vv_r(6)});
  vv_ID nid = vv_node(ctx, id)->id;
  bool open = (g_open_menu == nid);
  if (vv_clicked(ctx, id)) g_open_menu = open ? 0 : nid;
  else if (g_open_menu != 0 && g_open_menu != nid && vv_hovered(ctx, id)) g_open_menu = nid;
  // Re-read after the possible toggle so the highlight is immediate.
  open = (g_open_menu == nid);
  vv_Node *n = vv_node(ctx, id);
  n->target.bg = (open || vv_hovered(ctx, id)) ? t->surface_hi : (vv_Color){0};
  vv_text(ctx, label, (vv_Style){.fg = t->text, .font_size = t->font_size, .font = t->font});
  vv_end_box(ctx);
  return id;
}

bool vv_menu_is_open(vv_Ctx *ctx, uint32_t title_id) {
  return vv_node(ctx, title_id)->id == g_open_menu;
}

void vv_menu_begin(vv_Ctx *ctx, const char *key, vv_Vec2 at) {
  const vv_Theme *t = vv_theme();
  // Scrim: full-window catch so a click or Escape anywhere else dismisses.
  uint32_t scrim = vv_box_keyed(ctx, "__menuscrim", 11,
                                (vv_LayoutDecl){.has_absolute = true,
                                                .absolute = vv_rect(0, 0, ctx->win_w, ctx->win_h)},
                                (vv_Style){.bg = {0}});
  if (vv_pressed(ctx, scrim)) g_open_menu = 0;
  vv_end_box(ctx);
  for (int i = 0; i < ctx->input.key_count; i++)
    if (ctx->input.keys[i].key == VV_KEY_ESCAPE) g_open_menu = 0;

  vv_box_keyed(ctx, key, strlen(key),
               (vv_LayoutDecl){.dir = VV_COLUMN, .w = vv_fixed(220),
                               .padding = vv_all(5), .gap = 1,
                               .has_absolute = true,
                               .absolute = vv_rect(at.x, at.y + 2, 220, 0)},
               (vv_Style){.bg = t->surface_hi,
                          .radius = vv_r(8),
                          .border_width = vv_all(1),
                          .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                                     .offset = vv_v2(0, 6), .blur = 18, .spread = 2}});
}

bool vv_menu_item(vv_Ctx *ctx, const char *key, const char *label,
                  const char *shortcut) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->accent};
  uint32_t id = vv_box_keyed(ctx, key, strlen(key),
                             (vv_LayoutDecl){.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(28),
                                             .cross = VV_ALIGN_CENTER,
                                             .main = VV_ALIGN_SPACE_BETWEEN,
                                             .padding = vv_hv(10, 0), .focusable = true},
                             (vv_Style){.radius = vv_r(5), .hover = &hover});
  bool hot = vv_hovered(ctx, id);
  vv_text(ctx, label, (vv_Style){.fg = hot ? t->on_accent : t->text,
                                 .font_size = t->font_size, .font = t->font});
  if (shortcut && shortcut[0])
    vv_text(ctx, shortcut, (vv_Style){.fg = hot ? t->on_accent : t->text_muted,
                                      .font_size = t->font_size - 2, .font = t->font});
  vv_end_box(ctx);
  bool clicked = vv_clicked(ctx, id);
  if (clicked) g_open_menu = 0;
  return clicked;
}

void vv_menu_separator(vv_Ctx *ctx) {
  const vv_Theme *t = vv_theme();
  vv_box_keyed(ctx, "__sep", 5,
               (vv_LayoutDecl){.w = vv_grow(1), .h = vv_fixed(1), .padding = vv_hv(4, 0)},
               (vv_Style){.bg = t->border});
  vv_end_box(ctx);
}

void vv_menu_end(vv_Ctx *ctx) { vv_end_box(ctx); }

void vv_popover_begin(vv_Ctx *ctx, const char *key, vv_Vec2 at, float width,
                      vv_Msg close) {
  const vv_Theme *t = vv_theme();
  uint32_t scrim = vv_box_keyed(ctx, "__povscrim", 10,
                                (vv_LayoutDecl){.has_absolute = true,
                                                .absolute = vv_rect(0, 0, ctx->win_w, ctx->win_h)},
                                (vv_Style){.bg = {0}});
  if (vv_pressed(ctx, scrim)) vv_emit(ctx, close, VV_NO_PAYLOAD);
  vv_end_box(ctx);
  for (int i = 0; i < ctx->input.key_count; i++)
    if (ctx->input.keys[i].key == VV_KEY_ESCAPE) vv_emit(ctx, close, VV_NO_PAYLOAD);

  vv_box_keyed(ctx, key, strlen(key),
               (vv_LayoutDecl){.dir = VV_COLUMN, .w = vv_fixed(width),
                               .padding = vv_all(14), .gap = 10,
                               .has_absolute = true,
                               .absolute = vv_rect(at.x, at.y, width, 0)},
               (vv_Style){.bg = t->surface_hi,
                          .radius = vv_r(10),
                          .border_width = vv_all(1),
                          .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                                     .offset = vv_v2(0, 8), .blur = 22, .spread = 2}});
}
void vv_popover_end(vv_Ctx *ctx) { vv_end_box(ctx); }

typedef struct { float t; } TooltipState;

void vv_tooltip(vv_Ctx *ctx, uint32_t target_id, const char *text) {
  const vv_Theme *t = vv_theme();
  TooltipState *s = vv_state(ctx, target_id, TooltipState);
  bool hot = vv_hovered(ctx, target_id);
  if (!hot) { s->t = 0; return; }
  s->t += ctx->dt;
  if (s->t < 0.45f) { vv_invalidate(ctx); return; } // keep frames coming to time it

  vv_Rect r = vv_node(ctx, target_id)->actual_rect;
  float tx = r.x, ty = r.y + r.h + 6.0f;
  vv_box_keyed(ctx, vv_fmt(ctx, "__tip%u", target_id), 0,
               (vv_LayoutDecl){.padding = vv_hv(9, 5),
                               .has_absolute = true,
                               .absolute = vv_rect(tx, ty, 0, 0)},
               (vv_Style){.bg = vv_rgb(0.06f, 0.07f, 0.09f),
                          .radius = vv_r(6),
                          .border_width = vv_all(1),
                          .border_color = t->border,
                          .shadow = {.color = vv_rgba(0, 0, 0, 0.3f),
                                     .offset = vv_v2(0, 3), .blur = 10}});
  vv_text(ctx, text, (vv_Style){.fg = t->text, .font_size = t->font_size - 1, .font = t->font});
  vv_end_box(ctx);
}
