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

// Emit the optional hover/press/double-click bindings for a just-built node.
// Payloads for these auxiliary interactions default to none; encode context in
// the message id if needed. The primary action is emitted by each widget.
static void emit_aux(vv_Ctx *ctx, uint32_t id, vv_On on) {
  if (on.hover && vv_hovered(ctx, id)) vv_emit(ctx, on.hover, VV_NO_PAYLOAD);
  if (on.press && vv_pressed(ctx, id)) vv_emit(ctx, on.press, VV_NO_PAYLOAD);
  if (on.dbl && vv_double_clicked(ctx, id)) vv_emit(ctx, on.dbl, VV_NO_PAYLOAD);
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
  emit_aux(ctx, id, on);
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
  vv_Spring caret_x;  // animated caret position (px from text origin)
  bool init;
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

  // Animated caret position.
  float cx = measure_prefix(ctx, buf, s->cursor, size);
  if (!s->init) {
    vv_spring_init(&s->caret_x, cx, VV_SNAPPY);
    s->init = true;
  }
  vv_spring_retarget(&s->caret_x, cx);
  vv_spring_step(&s->caret_x, ctx->dt);

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
                                 .absolute = vv_rect(s->caret_x.x, 7, 2, 20)},
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
