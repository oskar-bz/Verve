// vv_theme.c — theme library + serialization (see vv_theme.h).
#include "verve/vv_theme.h"

#include <stdio.h>
#include <string.h>

// ---- introspection ---------------------------------------------------------
// Order here defines the file layout; append-only to stay compatible.
const vv_ThemeField vv_theme_fields[] = {
    {"surface", offsetof(vv_Theme, surface)},
    {"surface_hi", offsetof(vv_Theme, surface_hi)},
    {"accent", offsetof(vv_Theme, accent)},
    {"accent_hi", offsetof(vv_Theme, accent_hi)},
    {"accent_lo", offsetof(vv_Theme, accent_lo)},
    {"text", offsetof(vv_Theme, text)},
    {"text_muted", offsetof(vv_Theme, text_muted)},
    {"on_accent", offsetof(vv_Theme, on_accent)},
    {"track", offsetof(vv_Theme, track)},
    {"knob", offsetof(vv_Theme, knob)},
    {"border", offsetof(vv_Theme, border)},
    {"danger", offsetof(vv_Theme, danger)},
};
const int vv_theme_field_count = (int)(sizeof vv_theme_fields / sizeof vv_theme_fields[0]);

vv_Color vv_theme_field_get(const vv_Theme *t, int i) {
    return *(const vv_Color *)((const char *)t + vv_theme_fields[i].off);
}
void vv_theme_field_set(vv_Theme *t, int i, vv_Color c) {
    *(vv_Color *)((char *)t + vv_theme_fields[i].off) = c;
}

// ---- the library -----------------------------------------------------------
// Hex → linear-ish colours. We keep values in the same 0..1 sRGB space the
// rest of the renderer uses (vv_rgb), so #rrggbb maps component/255.
static vv_Color hex(unsigned rgb) {
    return vv_rgb((float)((rgb >> 16) & 0xff) / 255.0f,
                  (float)((rgb >> 8) & 0xff) / 255.0f,
                  (float)(rgb & 0xff) / 255.0f);
}

vv_Theme vv_theme_light(void) {
    return (vv_Theme){
        .surface = hex(0xffffff),   .surface_hi = hex(0xf0f0f2),
        .accent = hex(0x2f6fed),    .accent_hi = hex(0x4d84f2), .accent_lo = hex(0x1f57c8),
        .text = hex(0x1a1c22),      .text_muted = hex(0x6b7078), .on_accent = hex(0xffffff),
        .track = hex(0xd7dae0),     .knob = hex(0xffffff),       .border = hex(0xdadde3),
        .danger = hex(0xd23b30),    .radius = 8.0f, .font = 0, .font_size = 15.0f,
    };
}

// Classic Windows (9x / 2000): silver 3D chrome, navy selection, square corners.
vv_Theme vv_theme_win32(void) {
    return (vv_Theme){
        .surface = hex(0xc0c0c0),   .surface_hi = hex(0xd4d0c8),
        .accent = hex(0x000080),    .accent_hi = hex(0x1084d0), .accent_lo = hex(0x000060),
        .text = hex(0x000000),      .text_muted = hex(0x808080), .on_accent = hex(0xffffff),
        .track = hex(0xc0c0c0),     .knob = hex(0xdfdfdf),       .border = hex(0x808080),
        .danger = hex(0x800000),    .radius = 0.0f, .font = 0, .font_size = 13.0f,
    };
}

// WinUI / Fluent — Windows 11 light. Mica-grey surfaces, #0067c0 accent.
vv_Theme vv_theme_winui(void) {
    return (vv_Theme){
        .surface = hex(0xf9f9f9),   .surface_hi = hex(0xefefef),
        .accent = hex(0x0067c0),    .accent_hi = hex(0x1a75c5), .accent_lo = hex(0x005499),
        .text = hex(0x1b1b1b),      .text_muted = hex(0x5d5d5d), .on_accent = hex(0xffffff),
        .track = hex(0x868686),     .knob = hex(0xffffff),       .border = hex(0xe0e0e0),
        .danger = hex(0xc42b1c),    .radius = 6.0f, .font = 0, .font_size = 14.0f,
    };
}
// WinUI / Fluent — Windows 11 dark. Dark accent uses black on-accent text.
vv_Theme vv_theme_winui_dark(void) {
    return (vv_Theme){
        .surface = hex(0x272727),   .surface_hi = hex(0x323232),
        .accent = hex(0x4cc2ff),    .accent_hi = hex(0x60cdff), .accent_lo = hex(0x3aa0d8),
        .text = hex(0xf3f3f3),      .text_muted = hex(0xa0a0a0), .on_accent = hex(0x000000),
        .track = hex(0x9a9a9a),     .knob = hex(0xffffff),       .border = hex(0x3d3d3d),
        .danger = hex(0xff99a4),    .radius = 6.0f, .font = 0, .font_size = 14.0f,
    };
}

// GNOME Adwaita light: #3584e4 blue, warm-grey chrome, generous rounding.
vv_Theme vv_theme_adwaita(void) {
    return (vv_Theme){
        .surface = hex(0xffffff),   .surface_hi = hex(0xebebeb),
        .accent = hex(0x3584e4),    .accent_hi = hex(0x4a90e4), .accent_lo = hex(0x1c71d8),
        .text = hex(0x2e3436),      .text_muted = hex(0x5e5c64), .on_accent = hex(0xffffff),
        .track = hex(0xd4d4d4),     .knob = hex(0xffffff),       .border = hex(0xdcdcdc),
        .danger = hex(0xe01b24),    .radius = 9.0f, .font = 0, .font_size = 14.0f,
    };
}
vv_Theme vv_theme_adwaita_dark(void) {
    return (vv_Theme){
        .surface = hex(0x2d2d2d),   .surface_hi = hex(0x3a3a3a),
        .accent = hex(0x3584e4),    .accent_hi = hex(0x62a0ea), .accent_lo = hex(0x1c71d8),
        .text = hex(0xededed),      .text_muted = hex(0x9a9996), .on_accent = hex(0xffffff),
        .track = hex(0x1b1b1b),     .knob = hex(0xdedede),       .border = hex(0x1b1b1b),
        .danger = hex(0xff7b63),    .radius = 9.0f, .font = 0, .font_size = 14.0f,
    };
}

// Nord — a popular cool-toned dark palette.
vv_Theme vv_theme_nord(void) {
    return (vv_Theme){
        .surface = hex(0x2e3440),   .surface_hi = hex(0x3b4252),
        .accent = hex(0x88c0d0),    .accent_hi = hex(0x8fbcbb), .accent_lo = hex(0x5e81ac),
        .text = hex(0xeceff4),      .text_muted = hex(0x9aa4b8), .on_accent = hex(0x2e3440),
        .track = hex(0x434c5e),     .knob = hex(0xe5e9f0),       .border = hex(0x434c5e),
        .danger = hex(0xbf616a),    .radius = 7.0f, .font = 0, .font_size = 15.0f,
    };
}

// Solarized light — the classic Ethan Schoonover base3/blue palette.
vv_Theme vv_theme_solarized_light(void) {
    return (vv_Theme){
        .surface = hex(0xfdf6e3),   .surface_hi = hex(0xeee8d5),
        .accent = hex(0x268bd2),    .accent_hi = hex(0x2aa198), .accent_lo = hex(0x1c6fa8),
        .text = hex(0x073642),      .text_muted = hex(0x93a1a1), .on_accent = hex(0xfdf6e3),
        .track = hex(0xd9d2b8),     .knob = hex(0xfdf6e3),       .border = hex(0xd9d2b8),
        .danger = hex(0xdc322f),    .radius = 6.0f, .font = 0, .font_size = 15.0f,
    };
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
        n += snprintf(buf + (n < cap ? n : cap), (size_t)(n < cap ? cap - n : 0),
                      "%s %.4f %.4f %.4f\n", vv_theme_fields[i].name,
                      (double)c.r, (double)c.g, (double)c.b);
    }
    n += snprintf(buf + (n < cap ? n : cap), (size_t)(n < cap ? cap - n : 0),
                  "radius %.2f\nfont_size %.2f\n",
                  (double)t->radius, (double)t->font_size);
    return n;
}

int vv_theme_parse(vv_Theme *t, const char *text) {
    int found = 0;
    const char *p = text;
    char name[64];
    float x, y, z;
    while (*p) {
        // One record per line: "<name> f [f f]".
        int consumed = 0;
        int got = sscanf(p, "%63s %f %f %f%n", name, &x, &y, &z, &consumed);
        if (got >= 2) {
            if (strcmp(name, "radius") == 0) {
                t->radius = x;
            } else if (strcmp(name, "font_size") == 0) {
                t->font_size = x;
            } else if (got >= 4) {
                for (int i = 0; i < vv_theme_field_count; i++)
                    if (strcmp(name, vv_theme_fields[i].name) == 0) {
                        vv_theme_field_set(t, i, vv_rgb(x, y, z));
                        found++;
                        break;
                    }
            }
        }
        // Advance to the next line.
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
        (void)consumed;
    }
    return found;
}

bool vv_theme_save(const vv_Theme *t, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    // Size the buffer generously; the format is tiny and fixed-width.
    char buf[1024];
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
