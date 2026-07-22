// vv_theme.c — theme library + serialization (see vv_theme.h).
#include "verve/vv_theme.h"

#include <stdio.h>
#include <string.h>

// ---- introspection ---------------------------------------------------------
// The design tokens, grouped by category. Names double as the serialized keys.
// Order here defines the file layout; append-only to stay compatible. Legacy
// palette names (surface, accent, …) still load via the alias table below.
const vv_ThemeField vv_theme_fields[] = {
    {"surface_app", "Surfaces", offsetof(vv_Theme, surface_app)},
    {"surface_panel", "Surfaces", offsetof(vv_Theme, surface_panel)},
    {"surface_card", "Surfaces", offsetof(vv_Theme, surface_card)},
    {"surface_overlay", "Surfaces", offsetof(vv_Theme, surface_overlay)},

    {"control_bg_rest", "Controls", offsetof(vv_Theme, control_bg_rest)},
    {"control_bg_hover", "Controls", offsetof(vv_Theme, control_bg_hover)},
    {"control_bg_active", "Controls", offsetof(vv_Theme, control_bg_active)},
    {"control_bg_disabled", "Controls", offsetof(vv_Theme, control_bg_disabled)},

    {"text_primary", "Text", offsetof(vv_Theme, text_primary)},
    {"text_secondary", "Text", offsetof(vv_Theme, text_secondary)},
    {"text_muted", "Text", offsetof(vv_Theme, text_muted)},
    {"text_inverse", "Text", offsetof(vv_Theme, text_inverse)},
    {"text_on_brand", "Text", offsetof(vv_Theme, text_on_brand)},

    {"border_subtle", "Borders", offsetof(vv_Theme, border_subtle)},
    {"border_default", "Borders", offsetof(vv_Theme, border_default)},
    {"border_strong", "Borders", offsetof(vv_Theme, border_strong)},
    {"border_focus", "Borders", offsetof(vv_Theme, border_focus)},

    {"brand_primary", "Brand", offsetof(vv_Theme, brand_primary)},
    {"brand_hover", "Brand", offsetof(vv_Theme, brand_hover)},
    {"brand_active", "Brand", offsetof(vv_Theme, brand_active)},
    {"brand_subtle", "Brand", offsetof(vv_Theme, brand_subtle)},

    {"status_error", "Status", offsetof(vv_Theme, status_error)},
    {"status_error_subtle", "Status", offsetof(vv_Theme, status_error_subtle)},
    {"status_warning", "Status", offsetof(vv_Theme, status_warning)},
    {"status_success", "Status", offsetof(vv_Theme, status_success)},
    {"status_info", "Status", offsetof(vv_Theme, status_info)},

    {"knob", "Controls", offsetof(vv_Theme, knob)},
};
const int vv_theme_field_count = (int)(sizeof vv_theme_fields / sizeof vv_theme_fields[0]);

// Old palette names -> current token names, so .vvtheme files written against
// the smaller legacy palette still load.
static const struct { const char *old, *neu; } vv_theme_field_aliases[] = {
    {"surface", "surface_app"},   {"surface_hi", "control_bg_hover"},
    {"accent", "brand_primary"},  {"accent_hi", "brand_hover"},
    {"accent_lo", "brand_active"},{"text", "text_primary"},
    {"on_accent", "text_on_brand"},{"track", "control_bg_rest"},
    {"border", "border_default"}, {"danger", "status_error"},
};

vv_Color vv_theme_field_get(const vv_Theme *t, int i) {
    return *(const vv_Color *)((const char *)t + vv_theme_fields[i].off);
}
void vv_theme_field_set(vv_Theme *t, int i, vv_Color c) {
    *(vv_Color *)((char *)t + vv_theme_fields[i].off) = c;
}

// Scalar metrics: the radius + spacing scales, then the functional metrics.
// "radius"/"font_size" keep their historical names so older .vvtheme files
// still load; "radius" is the same storage as radius_md.
const vv_ThemeMetric vv_theme_metrics[] = {
    {"radius", "Radius", offsetof(vv_Theme, radius_md), 0.0f, 24.0f},
    {"radius_sm", "Radius", offsetof(vv_Theme, radius_sm), 0.0f, 12.0f},
    {"radius_lg", "Radius", offsetof(vv_Theme, radius_lg), 0.0f, 32.0f},
    {"radius_full", "Radius", offsetof(vv_Theme, radius_full), 0.0f, 9999.0f},
    {"space_xs", "Spacing", offsetof(vv_Theme, space_xs), 0.0f, 8.0f},
    {"space_sm", "Spacing", offsetof(vv_Theme, space_sm), 0.0f, 16.0f},
    {"space_md", "Spacing", offsetof(vv_Theme, space_md), 0.0f, 24.0f},
    {"space_lg", "Spacing", offsetof(vv_Theme, space_lg), 0.0f, 48.0f},
    {"border_width", "Metrics", offsetof(vv_Theme, border_width), 0.0f, 4.0f},
    {"pad_x", "Metrics", offsetof(vv_Theme, pad_x), 0.0f, 30.0f},
    {"pad_y", "Metrics", offsetof(vv_Theme, pad_y), 0.0f, 24.0f},
    {"gap", "Metrics", offsetof(vv_Theme, gap), 0.0f, 24.0f},
    {"font_size", "Metrics", offsetof(vv_Theme, font_size), 10.0f, 24.0f},
};
const int vv_theme_metric_count = (int)(sizeof vv_theme_metrics / sizeof vv_theme_metrics[0]);

float vv_theme_metric_get(const vv_Theme *t, int i) {
    return *(const float *)((const char *)t + vv_theme_metrics[i].off);
}
void vv_theme_metric_set(vv_Theme *t, int i, float v) {
    *(float *)((char *)t + vv_theme_metrics[i].off) = v;
}

// ---- the library -----------------------------------------------------------
// Hex → linear-ish colours. We keep values in the same 0..1 sRGB space the
// rest of the renderer uses (vv_rgb), so #rrggbb maps component/255.
static vv_Color hex(unsigned rgb) {
    return vv_rgb((float)((rgb >> 16) & 0xff) / 255.0f,
                  (float)((rgb >> 8) & 0xff) / 255.0f,
                  (float)(rgb & 0xff) / 255.0f);
}

// ---- token derivation ------------------------------------------------------
// A palette constructor supplies only the core roles (a dozen colours + the
// base metrics); vv_theme_complete fills the rest so every palette exposes a
// full, coherent token set without hand-authoring 26 colours nine times over.
static vv_Color mix(vv_Color a, vv_Color b, float t) {
    return vv_rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                   a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}
static float luma(vv_Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
static bool is_zero(vv_Color c) { return c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0; }
// Set `*dst` only if the palette left it unset (transparent black) — lets a
// constructor override any derived token by giving it a non-zero value.
static void deflt(vv_Color *dst, vv_Color v) { if (is_zero(*dst)) *dst = v; }

vv_Theme vv_theme_complete(vv_Theme t) {
    bool dark = luma(t.surface_app) < 0.5f;
    vv_Color fg = dark ? vv_rgb(1, 1, 1) : vv_rgb(0, 0, 0); // elevation direction

    // Surfaces: increasing elevation. Dark themes lift toward white; light
    // themes keep cards/overlays near-white and only tint the flat panel.
    if (dark) {
        deflt(&t.surface_panel,   mix(t.surface_app, fg, 0.05f));
        deflt(&t.surface_card,    mix(t.surface_app, fg, 0.09f));
        deflt(&t.surface_overlay, mix(t.surface_app, fg, 0.13f));
    } else {
        deflt(&t.surface_panel,   mix(t.surface_app, vv_rgb(0, 0, 0), 0.04f));
        deflt(&t.surface_card,    t.surface_app);
        deflt(&t.surface_overlay, t.surface_app);
    }

    // Interactive controls. rest/hover come from the core palette (track /
    // control_bg_hover); derive the pressed and disabled fills.
    deflt(&t.control_bg_active,   mix(t.control_bg_rest, fg, 0.16f));
    deflt(&t.control_bg_disabled, mix(t.surface_app, t.control_bg_rest, 0.4f));

    // Text ramp between primary and muted; inverse for inverted backgrounds.
    deflt(&t.text_secondary, mix(t.text_primary, t.text_muted, 0.5f));
    deflt(&t.text_inverse,   dark ? vv_rgb(0.10f, 0.10f, 0.12f)
                                  : vv_rgb(0.97f, 0.97f, 0.98f));

    // Borders around the default edge.
    deflt(&t.border_subtle, mix(t.border_default, t.surface_app, 0.5f));
    deflt(&t.border_strong, mix(t.border_default, fg, 0.35f));
    deflt(&t.border_focus,  t.brand_primary);

    // Brand tint for selection fills.
    deflt(&t.brand_subtle, mix(t.surface_app, t.brand_primary, dark ? 0.22f : 0.14f));

    // Status hues. error comes from the core palette (danger); the rest are
    // standard semantic colours, brightened a touch on dark backgrounds.
    deflt(&t.status_error_subtle, mix(t.surface_app, t.status_error, 0.18f));
    deflt(&t.status_warning, dark ? hex(0xf6c343) : hex(0xe5a50a));
    deflt(&t.status_success, dark ? hex(0x57e389) : hex(0x2ec27e));
    deflt(&t.status_info,    dark ? hex(0x62a0ea) : hex(0x3584e4));

    if (is_zero(t.knob)) t.knob = dark ? vv_rgb(0.95f, 0.96f, 0.98f) : vv_rgb(1, 1, 1);

    // Radius scale derived from the base (radius_md); pills stay pills.
    if (t.radius_sm == 0)   t.radius_sm = t.radius_md * 0.5f;
    if (t.radius_lg == 0)   t.radius_lg = t.radius_md * 1.75f;
    if (t.radius_full == 0) t.radius_full = 9999.0f;

    // Spacing scale derived from the base control padding / gap.
    if (t.space_xs == 0) t.space_xs = t.pad_y * 0.4f;
    if (t.space_sm == 0) t.space_sm = t.pad_y;
    if (t.space_md == 0) t.space_md = t.pad_x;
    if (t.space_lg == 0) t.space_lg = t.pad_x * 2.0f;
    return t;
}

// Each preset now authors the full design-token set in the token vocabulary —
// surface elevations, control states, the text/border ramps, brand and status
// hues — with values true to the design language it mimics. vv_theme_complete
// still runs, but only to fill the radius/spacing scales (and to backstop any
// token a palette deliberately leaves for derivation).

// Verve Light — clean neutral light, indigo-blue accent.
vv_Theme vv_theme_light(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0xffffff), .surface_panel = hex(0xf4f5f7),
        .surface_card = hex(0xffffff), .surface_overlay = hex(0xffffff),
        .control_bg_rest = hex(0xeef0f3), .control_bg_hover = hex(0xe4e7ec),
        .control_bg_active = hex(0xd7dbe2), .control_bg_disabled = hex(0xf2f3f5),
        .text_primary = hex(0x1a1c22), .text_secondary = hex(0x454a54),
        .text_muted = hex(0x8a9099), .text_inverse = hex(0xffffff), .text_on_brand = hex(0xffffff),
        .border_subtle = hex(0xeceef1), .border_default = hex(0xdadde3),
        .border_strong = hex(0xb9bec7), .border_focus = hex(0x2f6fed),
        .brand_primary = hex(0x2f6fed), .brand_hover = hex(0x4d84f2),
        .brand_active = hex(0x1f57c8), .brand_subtle = hex(0xdfe8fd),
        .status_error = hex(0xd23b30), .status_error_subtle = hex(0xfbe6e4),
        .status_warning = hex(0xb7791f), .status_success = hex(0x1f9d57), .status_info = hex(0x2f6fed),
        .knob = hex(0xffffff),
        .radius = 8.0f, .border_width = 1.0f, .pad_x = 14.0f, .pad_y = 9.0f, .gap = 10.0f, .font = 0, .font_size = 15.0f,
    });
}

// Classic Windows (9x / 2000): silver 3D chrome, navy selection, square corners.
vv_Theme vv_theme_win32(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0xc0c0c0), .surface_panel = hex(0xc0c0c0),
        .surface_card = hex(0xd4d0c8), .surface_overlay = hex(0xc0c0c0),
        .control_bg_rest = hex(0xffffff), .control_bg_hover = hex(0xd4d0c8),
        .control_bg_active = hex(0xa0a0a0), .control_bg_disabled = hex(0xc0c0c0),
        .text_primary = hex(0x000000), .text_secondary = hex(0x202020),
        .text_muted = hex(0x808080), .text_inverse = hex(0xffffff), .text_on_brand = hex(0xffffff),
        .border_subtle = hex(0xdfdfdf), .border_default = hex(0x808080),
        .border_strong = hex(0x404040), .border_focus = hex(0x000080),
        .brand_primary = hex(0x000080), .brand_hover = hex(0x1084d0),
        .brand_active = hex(0x000060), .brand_subtle = hex(0xc3d5f0),
        .status_error = hex(0x800000), .status_error_subtle = hex(0xe8c8c8),
        .status_warning = hex(0x808000), .status_success = hex(0x008000), .status_info = hex(0x000080),
        .knob = hex(0xdfdfdf),
        .radius = 0.0f, .border_width = 1.0f, .pad_x = 10.0f, .pad_y = 5.0f, .gap = 6.0f, .font = 0, .font_size = 13.0f,
    });
}

// WinUI / Fluent — Windows 11 light. Mica-grey surfaces, #0067c0 accent.
vv_Theme vv_theme_winui(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0xf3f3f3), .surface_panel = hex(0xeaeaea),
        .surface_card = hex(0xfbfbfb), .surface_overlay = hex(0xffffff),
        .control_bg_rest = hex(0xfbfbfb), .control_bg_hover = hex(0xf4f4f4),
        .control_bg_active = hex(0xededed), .control_bg_disabled = hex(0xf6f6f6),
        .text_primary = hex(0x1b1b1b), .text_secondary = hex(0x444444),
        .text_muted = hex(0x5d5d5d), .text_inverse = hex(0xffffff), .text_on_brand = hex(0xffffff),
        .border_subtle = hex(0xececec), .border_default = hex(0xe0e0e0),
        .border_strong = hex(0xc4c4c4), .border_focus = hex(0x0067c0),
        .brand_primary = hex(0x0067c0), .brand_hover = hex(0x1a75c5),
        .brand_active = hex(0x005499), .brand_subtle = hex(0xcfe4f7),
        .status_error = hex(0xc42b1c), .status_error_subtle = hex(0xfde7e9),
        .status_warning = hex(0x9d5d00), .status_success = hex(0x0f7b0f), .status_info = hex(0x0067c0),
        .knob = hex(0xffffff),
        .radius = 6.0f, .border_width = 1.0f, .pad_x = 16.0f, .pad_y = 10.0f, .gap = 12.0f, .font = 0, .font_size = 14.0f,
    });
}
// WinUI / Fluent — Windows 11 dark. Dark accent uses black on-accent text.
vv_Theme vv_theme_winui_dark(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0x202020), .surface_panel = hex(0x272727),
        .surface_card = hex(0x2c2c2c), .surface_overlay = hex(0x2d2d2d),
        .control_bg_rest = hex(0x2d2d2d), .control_bg_hover = hex(0x323232),
        .control_bg_active = hex(0x383838), .control_bg_disabled = hex(0x292929),
        .text_primary = hex(0xf3f3f3), .text_secondary = hex(0xcfcfcf),
        .text_muted = hex(0xa0a0a0), .text_inverse = hex(0x1b1b1b), .text_on_brand = hex(0x000000),
        .border_subtle = hex(0x303030), .border_default = hex(0x3d3d3d),
        .border_strong = hex(0x545454), .border_focus = hex(0x4cc2ff),
        .brand_primary = hex(0x4cc2ff), .brand_hover = hex(0x60cdff),
        .brand_active = hex(0x3aa0d8), .brand_subtle = hex(0x244d63),
        .status_error = hex(0xff99a4), .status_error_subtle = hex(0x442326),
        .status_warning = hex(0xf6c343), .status_success = hex(0x6ccb5f), .status_info = hex(0x60cdff),
        .knob = hex(0xffffff),
        .radius = 6.0f, .border_width = 1.0f, .pad_x = 16.0f, .pad_y = 10.0f, .gap = 12.0f, .font = 0, .font_size = 14.0f,
    });
}

// GNOME Adwaita light: #3584e4 blue, warm-grey chrome, generous rounding.
vv_Theme vv_theme_adwaita(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0xffffff), .surface_panel = hex(0xf6f5f4),
        .surface_card = hex(0xffffff), .surface_overlay = hex(0xffffff),
        .control_bg_rest = hex(0xf6f5f4), .control_bg_hover = hex(0xededed),
        .control_bg_active = hex(0xe0e0e0), .control_bg_disabled = hex(0xf4f4f4),
        .text_primary = hex(0x2e3436), .text_secondary = hex(0x4d4d4d),
        .text_muted = hex(0x5e5c64), .text_inverse = hex(0xffffff), .text_on_brand = hex(0xffffff),
        .border_subtle = hex(0xededed), .border_default = hex(0xdcdcdc),
        .border_strong = hex(0xc0c0c0), .border_focus = hex(0x3584e4),
        .brand_primary = hex(0x3584e4), .brand_hover = hex(0x4a90e4),
        .brand_active = hex(0x1c71d8), .brand_subtle = hex(0xd6e6fb),
        .status_error = hex(0xe01b24), .status_error_subtle = hex(0xfbe0e1),
        .status_warning = hex(0xe5a50a), .status_success = hex(0x2ec27e), .status_info = hex(0x3584e4),
        .knob = hex(0xffffff),
        .radius = 9.0f, .border_width = 1.0f, .pad_x = 16.0f, .pad_y = 10.0f, .gap = 12.0f, .font = 0, .font_size = 14.0f,
    });
}
vv_Theme vv_theme_adwaita_dark(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0x242424), .surface_panel = hex(0x303030),
        .surface_card = hex(0x303030), .surface_overlay = hex(0x383838),
        .control_bg_rest = hex(0x3a3a3a), .control_bg_hover = hex(0x444444),
        .control_bg_active = hex(0x4f4f4f), .control_bg_disabled = hex(0x2e2e2e),
        .text_primary = hex(0xededed), .text_secondary = hex(0xc5c5c5),
        .text_muted = hex(0x9a9996), .text_inverse = hex(0x242424), .text_on_brand = hex(0xffffff),
        .border_subtle = hex(0x2b2b2b), .border_default = hex(0x1b1b1b),
        .border_strong = hex(0x575757), .border_focus = hex(0x78aeed),
        .brand_primary = hex(0x3584e4), .brand_hover = hex(0x62a0ea),
        .brand_active = hex(0x1c71d8), .brand_subtle = hex(0x1a3a5c),
        .status_error = hex(0xff7b63), .status_error_subtle = hex(0x4a2420),
        .status_warning = hex(0xf6c343), .status_success = hex(0x57e389), .status_info = hex(0x62a0ea),
        .knob = hex(0xdedede),
        .radius = 9.0f, .border_width = 1.0f, .pad_x = 16.0f, .pad_y = 10.0f, .gap = 12.0f, .font = 0, .font_size = 14.0f,
    });
}

// Nord — the cool-toned palette: polar-night surfaces, frost accent, aurora status.
vv_Theme vv_theme_nord(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0x2e3440), .surface_panel = hex(0x3b4252),
        .surface_card = hex(0x434c5e), .surface_overlay = hex(0x434c5e),
        .control_bg_rest = hex(0x3b4252), .control_bg_hover = hex(0x434c5e),
        .control_bg_active = hex(0x4c566a), .control_bg_disabled = hex(0x333a48),
        .text_primary = hex(0xeceff4), .text_secondary = hex(0xd8dee9),
        .text_muted = hex(0x9aa4b8), .text_inverse = hex(0x2e3440), .text_on_brand = hex(0x2e3440),
        .border_subtle = hex(0x3b4252), .border_default = hex(0x434c5e),
        .border_strong = hex(0x4c566a), .border_focus = hex(0x88c0d0),
        .brand_primary = hex(0x88c0d0), .brand_hover = hex(0x8fbcbb),
        .brand_active = hex(0x5e81ac), .brand_subtle = hex(0x3b4a58),
        .status_error = hex(0xbf616a), .status_error_subtle = hex(0x463840),
        .status_warning = hex(0xebcb8b), .status_success = hex(0xa3be8c), .status_info = hex(0x81a1c1),
        .knob = hex(0xe5e9f0),
        .radius = 7.0f, .border_width = 1.0f, .pad_x = 14.0f, .pad_y = 9.0f, .gap = 10.0f, .font = 0, .font_size = 15.0f,
    });
}

// Solarized light — Ethan Schoonover's base3/blue palette with base00–03 text.
vv_Theme vv_theme_solarized_light(void) {
    return vv_theme_complete((vv_Theme){
        .surface_app = hex(0xfdf6e3), .surface_panel = hex(0xeee8d5),
        .surface_card = hex(0xfdf6e3), .surface_overlay = hex(0xeee8d5),
        .control_bg_rest = hex(0xeee8d5), .control_bg_hover = hex(0xe4ddc8),
        .control_bg_active = hex(0xd9d2b8), .control_bg_disabled = hex(0xf2ecd9),
        .text_primary = hex(0x073642), .text_secondary = hex(0x586e75),
        .text_muted = hex(0x93a1a1), .text_inverse = hex(0xfdf6e3), .text_on_brand = hex(0xfdf6e3),
        .border_subtle = hex(0xeee8d5), .border_default = hex(0xd9d2b8),
        .border_strong = hex(0x93a1a1), .border_focus = hex(0x268bd2),
        .brand_primary = hex(0x268bd2), .brand_hover = hex(0x2aa198),
        .brand_active = hex(0x1c6fa8), .brand_subtle = hex(0xd7e6ef),
        .status_error = hex(0xdc322f), .status_error_subtle = hex(0xf3ddd3),
        .status_warning = hex(0xb58900), .status_success = hex(0x859900), .status_info = hex(0x268bd2),
        .knob = hex(0xfdf6e3),
        .radius = 6.0f, .border_width = 1.0f, .pad_x = 14.0f, .pad_y = 9.0f, .gap = 10.0f, .font = 0, .font_size = 15.0f,
    });
}

const vv_ThemePreset vv_theme_presets[] = {
    {"Verve Dark", vv_theme_dark},
    {"Verve Light", vv_theme_light},
    {"Windows Classic", vv_theme_win32},
    {"WinUI Light", vv_theme_winui},
    {"WinUI Dark", vv_theme_winui_dark},
    {"Adwaita Light", vv_theme_adwaita},
    {"Adwaita Dark", vv_theme_adwaita_dark},
    {"Nord", vv_theme_nord},
    {"Solarized Light", vv_theme_solarized_light},
};
const int vv_theme_preset_count = (int)(sizeof vv_theme_presets / sizeof vv_theme_presets[0]);

// ---- serialization ---------------------------------------------------------
int vv_theme_write(const vv_Theme *t, char *buf, int cap) {
    int n = 0;
    for (int i = 0; i < vv_theme_field_count; i++) {
        vv_Color c = vv_theme_field_get(t, i);
        // "<name> r g b" for opaque colours; append a fourth alpha component
        // only when the colour is translucent, so opaque themes stay compact
        // and older three-component files keep their exact shape.
        if (c.a < 0.9995f)
            n += snprintf(buf + (n < cap ? n : cap), (size_t)(n < cap ? cap - n : 0),
                          "%s %.4f %.4f %.4f %.4f\n", vv_theme_fields[i].name,
                          (double)c.r, (double)c.g, (double)c.b, (double)c.a);
        else
            n += snprintf(buf + (n < cap ? n : cap), (size_t)(n < cap ? cap - n : 0),
                          "%s %.4f %.4f %.4f\n", vv_theme_fields[i].name,
                          (double)c.r, (double)c.g, (double)c.b);
    }
    for (int i = 0; i < vv_theme_metric_count; i++)
        n += snprintf(buf + (n < cap ? n : cap), (size_t)(n < cap ? cap - n : 0),
                      "%s %.2f\n", vv_theme_metrics[i].name,
                      (double)vv_theme_metric_get(t, i));
    return n;
}

int vv_theme_parse(vv_Theme *t, const char *text) {
    int found = 0;
    const char *p = text;
    char name[64];
    float x, y, z, w;
    while (*p) {
        // One record per line: "<name> f [f f [f]]" — a metric (one value) or a
        // colour (r g b, plus an optional fourth alpha component).
        int got = sscanf(p, "%63s %f %f %f %f", name, &x, &y, &z, &w);
        if (got >= 4) {
            // Colour field. Alpha defaults to opaque when omitted.
            float alpha = (got >= 5) ? w : 1.0f;
            // Resolve legacy names first.
            const char *fname = name;
            for (size_t a = 0; a < sizeof vv_theme_field_aliases / sizeof vv_theme_field_aliases[0]; a++)
                if (strcmp(name, vv_theme_field_aliases[a].old) == 0) {
                    fname = vv_theme_field_aliases[a].neu;
                    break;
                }
            for (int i = 0; i < vv_theme_field_count; i++)
                if (strcmp(fname, vv_theme_fields[i].name) == 0) {
                    vv_theme_field_set(t, i, vv_rgba(x, y, z, alpha));
                    found++;
                    break;
                }
        } else if (got >= 2) {
            // "<name> value" — a scalar metric (radius/border_width/pad/gap/...).
            for (int i = 0; i < vv_theme_metric_count; i++)
                if (strcmp(name, vv_theme_metrics[i].name) == 0) {
                    vv_theme_metric_set(t, i, x);
                    break;
                }
        }
        // Advance to the next line.
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return found;
}

bool vv_theme_save(const vv_Theme *t, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    // Size the buffer generously; the format is tiny and fixed-width.
    char buf[2048];
    int n = vv_theme_write(t, buf, (int)sizeof buf);
    if (n < 0 || n >= (int)sizeof buf) { fclose(f); return false; }
    size_t w = fwrite(buf, 1, (size_t)n, f);
    fclose(f);
    return w == (size_t)n;
}

bool vv_theme_load(vv_Theme *t, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return vv_theme_parse(t, buf) > 0;
}
