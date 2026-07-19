// vv_widgets.h — the built-in widget catalogue (§14.5).
//
// Every widget is a plain function written in the public API (§14.1): there is
// no widget type, base class, or registry. These are the acceptance test for
// the API — if a built-in needs internal access, the API is wrong. C widgets
// take values + a key and return interaction/edited values.
#ifndef VV_WIDGETS_H
#define VV_WIDGETS_H

#include "vv_context.h"
#include "vv_value.h"

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

// Bind hover/press/double-click/move interactions to messages on any node
// (typically a plain vv_box). hover/press/dbl fire this frame; `move` emits the
// cursor position while the node is hovered and is the opt-in that makes a
// cursor-driven view rebuild on motion. Call right after building the box.
void vv_on(vv_Ctx *ctx, uint32_t id, vv_On on);

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

// Value-bound variants (§12). Instead of a message, these take a vv_Value: the
// widget reads the current value to render and, on change, emits VV_MSG_BIND so
// the driver writes it back through the bound pointer (curve/min/max/READONLY
// from the value's metadata are honored). No update() case needed — bind events
// apply automatically. Drag widgets bracket the drag as one edit (§12.1).
uint32_t vv_slider_bound(vv_Ctx *ctx, const char *key, vv_Value v);
uint32_t vv_drag_number_bound(vv_Ctx *ctx, const char *key, vv_Value v, float speed);
uint32_t vv_checkbox_bound(vv_Ctx *ctx, const char *key, const char *label, vv_Value v);
uint32_t vv_toggle_bound(vv_Ctx *ctx, const char *key, vv_Value v);

// Single-line editable text field. Edits `buf` (NUL-terminated, capacity `cap`)
// in place and, on any change this frame, emits `change` with `.as_str = buf`.
// Animated caret; arrow/home/end nav, shift-selection, clipboard via backend.
uint32_t vv_text_field(vv_Ctx *ctx, const char *key, char *buf, int cap,
                       const char *placeholder, vv_Msg change);

// Multi-line editor. Like vv_text_field but Enter inserts a newline, Up/Down
// move between lines (keeping a goal column), Home/End act per line, and the
// content scrolls vertically inside a box of fixed `height`. Multi-line
// selection is drawn per row. Emits `change` (`.as_str = buf`) on any edit.
uint32_t vv_text_area(vv_Ctx *ctx, const char *key, char *buf, int cap,
                      float height, const char *placeholder, vv_Msg change);

// Calendar date field. Shows `date` (packed as year*10000 + month*100 + day,
// e.g. 20260719) and opens a month-grid popover to pick a new one. All of its
// internal interaction — open/close, prev/next month, day hover — is kept in the
// field's own node state and emits nothing; only choosing a day emits `change`
// with the new packed date in `.as_int`. A controlled widget: pass the current
// date, store the emitted one. `vv_date_pack`/`vv_date_unpack` help at the edges.
uint32_t vv_date_field(vv_Ctx *ctx, const char *key, int32_t date, vv_Msg change);
static inline int32_t vv_date_pack(int y, int m, int d) { return y * 10000 + m * 100 + d; }
static inline void    vv_date_unpack(int32_t v, int *y, int *m, int *d) {
    if (y) *y = v / 10000; if (m) *m = (v / 100) % 100; if (d) *d = v % 100;
}

// A draggable divider for resizable multi-panel layouts. Place it between two
// panes inside a VV_ROW (a vertical bar that resizes horizontally) or a
// VV_COLUMN (a horizontal bar, vertically). `size` is the current size of the
// resized pane — kept in app state and set as that pane's fixed size. Set
// `trailing` when the pane is *after* the divider (a right/bottom dock): then a
// rightward/downward drag shrinks it, and you still pass a plain positive size.
// While dragged it emits `resize` with the new positive size in `.as_float`,
// clamped to [min,max]; update() writes it back. Panes FLIP-spring, so the
// resize (and any reset) animates for free.
uint32_t vv_splitter(vv_Ctx *ctx, const char *key, vv_Axis dir, bool trailing,
                     float size, float min, float max, vv_Msg resize);

// ---- overlay chrome: menus, popovers, tooltips (§ in-app overlay) ----------
// Overlays set a layout `.z` so the painter lifts them above the normal tree
// (any node with z>0 is drawn last, in ascending z). That means menus, popovers
// and tooltips can be declared *inline* — wherever they're logically relevant —
// and still paint on top; they no longer have to be built last in the view.
enum { VV_Z_MENU = 1000, VV_Z_POPOVER = 1000, VV_Z_TOOLTIP = 2000 };

// A top menu strip. Put vv_menu_title()s between begin/end.
void     vv_menubar_begin(vv_Ctx *ctx);
void     vv_menubar_end(vv_Ctx *ctx);
// A clickable title in the bar. Self-managing: clicking opens/closes its menu,
// and once any menu is open, hovering another title switches to it. Returns the
// node handle; read its actual_rect to position the dropdown.
uint32_t vv_menu_title(vv_Ctx *ctx, const char *key, const char *label);
// True while `title_id`'s menu is open — build its dropdown then (it z-lifts).
bool     vv_menu_is_open(vv_Ctx *ctx, uint32_t title_id);

// The dropdown panel at screen point `at` (declare it right after the title).
// Includes a full-window scrim so clicking away (or Escape) dismisses. Items go
// between.
void     vv_menu_begin(vv_Ctx *ctx, const char *key, vv_Vec2 at);
bool     vv_menu_item(vv_Ctx *ctx, const char *key, const char *label,
                      const char *shortcut); // true when chosen (closes the menu)
void     vv_menu_separator(vv_Ctx *ctx);
void     vv_menu_end(vv_Ctx *ctx);

// A free-floating popover panel anchored at `at`, `width` wide (z-lifts, so
// declare it inline). App owns the open flag; a scrim emits `close` on
// outside-click or Escape. Put content between begin/end.
void     vv_popover_begin(vv_Ctx *ctx, const char *key, vv_Vec2 at, float width,
                          vv_Msg close);
// Same, but dismiss flips `*open` to false directly — pair with vv_ui_state for
// a popover that needs no app-state field, message, or update() case.
void     vv_popover_open(vv_Ctx *ctx, const char *key, vv_Vec2 at, float width,
                         bool *open);
void     vv_popover_end(vv_Ctx *ctx);

// Hover tooltip for `target_id`: after a short hover it fades in a small label
// below the node. Self-contained (hover timer in node state) and z-lifted, so
// call it anywhere. Keeps frames alive while timing.
void     vv_tooltip(vv_Ctx *ctx, uint32_t target_id, const char *text);

// Labelled helpers.
void  vv_label(vv_Ctx *ctx, const char *text);
void  vv_label_muted(vv_Ctx *ctx, const char *text);

#endif // VV_WIDGETS_H
