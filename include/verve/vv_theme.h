// vv_theme.h — the theme library: named palettes + text (de)serialization.
//
// A theme is "just values" (§7.1): a flat struct of colours plus radius and
// font metrics. That makes two things cheap and generic:
//
//   • Serialization — every colour field is reachable by name + offset, so one
//     small loop reads/writes a whole palette. `.vvtheme` files are plain text
//     ("<field> r g b" per line, plus "radius R" / "font_size S"), so any app
//     can load and apply a theme at runtime (see vv_theme_load).
//   • A library — recognizable desktop looks (classic Win32, Fluent/WinUI,
//     GNOME Adwaita) and a couple of popular editor palettes, each a plain
//     constructor. Swapping one animates for free (springs on every colour).
#ifndef VV_THEME_H
#define VV_THEME_H

#include "vv_widgets.h" // vv_Theme
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- introspection ---------------------------------------------------------
// The colour fields of vv_Theme, by name + byte offset. Iterate these to build
// a serializer or a generic colour editor without hard-coding the field list.
typedef struct {
    const char *name;
    size_t      off; // offsetof(vv_Theme, <field>)
} vv_ThemeField;

extern const vv_ThemeField vv_theme_fields[];
extern const int           vv_theme_field_count;

// Read/write a colour field by index (0..vv_theme_field_count-1).
vv_Color vv_theme_field_get(const vv_Theme *t, int i);
void     vv_theme_field_set(vv_Theme *t, int i, vv_Color c);

// The scalar shape/spacing metrics (radius, border width, padding, gap, font
// size), by name + offset + a sensible editor range. Iterate these to build a
// generic metric editor or serialize the non-colour side of a theme.
typedef struct {
    const char *name;
    size_t      off;       // offsetof(vv_Theme, <float field>)
    float       lo, hi;    // suggested slider range
} vv_ThemeMetric;

extern const vv_ThemeMetric vv_theme_metrics[];
extern const int            vv_theme_metric_count;

float vv_theme_metric_get(const vv_Theme *t, int i);
void  vv_theme_metric_set(vv_Theme *t, int i, float v);

// ---- the built-in palette library ------------------------------------------
// vv_theme_dark() is declared in vv_widgets.h (it is also the default palette).
vv_Theme vv_theme_light(void);         // clean neutral light
vv_Theme vv_theme_win32(void);         // classic Windows 9x/2000 3D grey
vv_Theme vv_theme_winui(void);         // WinUI / Fluent — Windows 11 light
vv_Theme vv_theme_winui_dark(void);    // WinUI / Fluent — Windows 11 dark
vv_Theme vv_theme_adwaita(void);       // GNOME Adwaita light
vv_Theme vv_theme_adwaita_dark(void);  // GNOME Adwaita dark
vv_Theme vv_theme_nord(void);          // Nord (popular cool dark)
vv_Theme vv_theme_solarized_light(void); // Solarized light

// A named entry in the library, for building pickers ("apply theme" menus).
typedef struct {
    const char *name;
    vv_Theme (*make)(void);
} vv_ThemePreset;

extern const vv_ThemePreset vv_theme_presets[];
extern const int            vv_theme_preset_count;

// ---- text (de)serialization ------------------------------------------------
// Write `t` to `buf` (up to `cap` bytes); returns bytes that would be written
// (may exceed cap, like snprintf). `vv_theme_parse` fills `t` from text and
// returns the number of colour fields it recognized.
int  vv_theme_write(const vv_Theme *t, char *buf, int cap);
int  vv_theme_parse(vv_Theme *t, const char *text);

// File variants. Both return true on success. `vv_theme_load` leaves fields it
// does not find untouched, so callers should start from a sensible base
// (e.g. vv_theme_dark()) if the file may be partial.
bool vv_theme_save(const vv_Theme *t, const char *path);
bool vv_theme_load(vv_Theme *t, const char *path);

#ifdef __cplusplus
}
#endif

#endif // VV_THEME_H
