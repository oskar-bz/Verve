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

// Primitives / interactive (§14.5). `key` gives stable identity in loops and
// conditionals; pass NULL to rely on sequence.
bool  vv_button(vv_Ctx *ctx, const char *key, const char *label);
bool  vv_toggle(vv_Ctx *ctx, const char *key, bool value);            // returns new value
bool  vv_checkbox(vv_Ctx *ctx, const char *key, const char *label, bool value);
float vv_slider(vv_Ctx *ctx, const char *key, float value, float min, float max);
float vv_drag_number(vv_Ctx *ctx, const char *key, float value, float speed,
                     float min, float max);
// A selectable list row; returns true if clicked this frame.
bool  vv_list_item(vv_Ctx *ctx, const char *key, const char *label, bool selected);

// Labelled helpers.
void  vv_label(vv_Ctx *ctx, const char *text);
void  vv_label_muted(vv_Ctx *ctx, const char *text);

#endif // VV_WIDGETS_H
