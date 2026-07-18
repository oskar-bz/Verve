// vv_widgets.h — the built-in widget catalogue (§14.5).
//
// Every widget is a plain function written in the public API (§14.1): there is
// no widget type, base class, or registry. These are the acceptance test for
// the API — if a built-in needs internal access, the API is wrong. C widgets
// take values + a key and return interaction/edited values.
#ifndef VV_WIDGETS_H
#define VV_WIDGETS_H

#include "vv_context.h"

// A theme is just values (§7.1). Swapping one animates for free.
typedef struct {
    vv_Color surface, surface_hi;
    vv_Color accent, accent_hi, accent_lo;
    vv_Color text, text_muted, on_accent;
    vv_Color track, knob, border, danger;
    float    radius;
    vv_FontID font;
    float     font_size;
} vv_Theme;

vv_Theme       vv_theme_dark(void);   // sensible default palette
void           vv_set_theme(const vv_Theme *t);
const vv_Theme *vv_theme(void);

// Primitives / interactive (§14.5). Widgets are emit-only: rather than return
// an interaction value, they push a message + payload into the context queue
// when their action fires, and return their node handle (for vv_state or extra
// queries). `key` gives stable identity in loops/conditionals; NULL => sequence.
//
// The primary action is a direct `on_*` message parameter; the `_on` variants
// take an optional vv_On for hover/press/double-click bindings. Controlled
// widgets (checkbox, toggle, slider) take the *current* value to render and
// emit the *new* value in the payload — update() writes it back to state.

// Emits `click` (payload `arg`) when pressed and released inside.
uint32_t vv_button(vv_Ctx *ctx, const char *key, const char *label,
                   vv_Msg click, vv_Payload arg);
uint32_t vv_button_on(vv_Ctx *ctx, const char *key, const char *label,
                      vv_Msg click, vv_Payload arg, vv_On on);

// Emit `change` with `.as_int = !value` when clicked.
uint32_t vv_toggle(vv_Ctx *ctx, const char *key, bool value, vv_Msg change);
uint32_t vv_checkbox(vv_Ctx *ctx, const char *key, const char *label,
                     bool value, vv_Msg change);

// Emit `change` with `.as_float = new_value` while dragged.
uint32_t vv_slider(vv_Ctx *ctx, const char *key, float value, float min,
                   float max, vv_Msg change);
uint32_t vv_drag_number(vv_Ctx *ctx, const char *key, float value, float speed,
                        float min, float max, vv_Msg change);

// A selectable list row; emits `click` (payload `arg`) when clicked.
uint32_t vv_list_item(vv_Ctx *ctx, const char *key, const char *label,
                      bool selected, vv_Msg click, vv_Payload arg);

// Single-line editable text field. Edits `buf` (NUL-terminated, capacity `cap`)
// in place and, on any change this frame, emits `change` with `.as_str = buf`.
// Animated caret; arrow/home/end nav, shift-selection, clipboard via backend.
uint32_t vv_text_field(vv_Ctx *ctx, const char *key, char *buf, int cap,
                       const char *placeholder, vv_Msg change);

// Labelled helpers.
void  vv_label(vv_Ctx *ctx, const char *text);
void  vv_label_muted(vv_Ctx *ctx, const char *text);

#endif // VV_WIDGETS_H
