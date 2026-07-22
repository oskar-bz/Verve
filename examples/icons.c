// icons.c — the craz vector integration in one screen:
//   • tintable SVG icons: one A8 coverage mask per size, recoloured for free on
//     the GPU (the same mask is drawn in every theme colour, no re-bake);
//   • a size sweep of one icon (each size bakes once, then caches);
//   • a full-colour SVG rasterized to a texture and cached;
//   • a dynamic craz canvas re-rasterized every frame (a spinning gradient star)
//     to show the app-driven path.
//
//   Build:  make icons   ->   ./build/icons
//
// A manual loop (not vv_app_run) because we need the app handle each frame — for
// vv_app_vector(), the display dpi, and to re-raster the animated canvas.
#include "vv_sdl_gl.h"
#include "vv_vector.h"
#include "craz/craz.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// A few 24x24 monochrome icons (fill paths). Their colours are irrelevant — the
// icon path keeps only shape coverage and tints on the GPU.
static const char *SVG_HEART =
    "<svg viewBox='0 0 24 24'><path d='M12 21s-7.5-4.6-10-9.3C0.3 8.6 1.6 5 5 5c2 0 3.2 1.1 "
    "4 2.3C9.8 6.1 11 5 13 5c3.4 0 4.7 3.6 3 6.7C19.5 16.4 12 21 12 21z'/></svg>";
static const char *SVG_STAR =
    "<svg viewBox='0 0 24 24'><path d='M12 2l3 6.9 7.5.6-5.7 4.9 1.8 7.3L12 17.8 5.4 21.7 "
    "7.2 14.4 1.5 9.5 9 8.9z'/></svg>";
static const char *SVG_GEAR =
    "<svg viewBox='0 0 24 24'><path d='M12 8a4 4 0 100 8 4 4 0 000-8zm9 4a7 7 0 00-.1-1.2l2-1.6"
    "-2-3.4-2.4 1a7 7 0 00-2-1.2L16 2H8l-.5 2.6a7 7 0 00-2 1.2l-2.4-1-2 3.4 2 1.6A7 7 0 003 12"
    "c0 .4 0 .8.1 1.2l-2 1.6 2 3.4 2.4-1c.6.5 1.3.9 2 1.2L8 22h8l.5-2.6c.7-.3 1.4-.7 2-1.2l2.4 1 "
    "2-3.4-2-1.6c.1-.4.1-.8.1-1.2z'/></svg>";
static const char *SVG_CHECK =
    "<svg viewBox='0 0 24 24'><path d='M20.3 5.7L9 17 3.7 11.7 5.1 10.3 9 14.2 18.9 4.3z'/></svg>";
static const char *SVG_BOLT =
    "<svg viewBox='0 0 24 24'><path d='M13 2L3 14h7l-1 8 10-12h-7z'/></svg>";

typedef struct { const char *name; vv_Icon icon; } NamedIcon;

typedef struct {
    vv_App    *app;
    vv_Vector *vec;
    float      dpi;
    double     t;

    NamedIcon  icons[5];
    vv_SvgDoc *svg;      // full-colour sample (craz tiger if present)
    vv_Canvas *canvas;   // animated craz drawing
} App;

// ---- animated canvas: a spinning 5-point gradient star, drawn with craz ------
static void redraw_canvas(App *a, int size_dev) {
    if (!a->canvas) a->canvas = vv_vector_canvas(a->vec, size_dev, size_dev);
    vv_canvas_resize(a->canvas, size_dev, size_dev);

    cr_context *ctx; cr_surface *s;
    vv_canvas_begin(a->canvas, &ctx, &s);

    float cx = size_dev * 0.5f, cy = size_dev * 0.5f, R = size_dev * 0.42f, r = R * 0.42f;
    float rot = (float)a->t * 0.8f;
    cr_path *p = cr_path_new();
    for (int i = 0; i < 10; i++) {
        float ang = rot + (float)i * 3.14159265f / 5.0f - 1.5708f;
        float rad = (i & 1) ? r : R;
        float x = cx + cosf(ang) * rad, y = cy + sinf(ang) * rad;
        if (i == 0) cr_path_move_to(p, x, y); else cr_path_line_to(p, x, y);
    }
    cr_path_close(p);

    cr_gradient_stop stops[3] = {
        { 0.0f, cr_rgba(255, 210, 60, 255) },
        { 0.5f, cr_rgba(255, 90, 140, 255) },
        { 1.0f, cr_rgba(120, 90, 240, 255) },
    };
    cr_paint grad = cr_paint_radial(cx, cy, R, stops, 3);
    cr_fill_path(ctx, s, NULL, p, NULL, &grad, CR_FILL_NONZERO);

    cr_stroke_style st = { .width = size_dev * 0.02f, .cap = CR_CAP_ROUND, .join = CR_JOIN_ROUND };
    cr_paint edge = cr_paint_color(cr_rgba(255, 255, 255, 200));
    cr_stroke_path(ctx, s, NULL, p, NULL, &edge, &st);

    cr_path_free(p);
    vv_canvas_commit(a->canvas);
}

// ---- view ------------------------------------------------------------------
static void icon_row(vv_Ctx *c, App *a, vv_Icon icon, const char *name) {
    const vv_Theme *t = vv_theme();
    vv_Color colors[5] = { t->text_primary, t->brand_primary, t->status_error,
                           t->status_success, t->accent };
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 14, .cross = VV_ALIGN_CENTER,
                        .padding = vv_hv(4, 4)),
           VV_STYLE(.bg = {0})) {
        vv_text(c, name, VV_STYLE(.fg = t->text_secondary, .font_size = 13));
        VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
        for (int i = 0; i < 5; i++) {
            char key[32]; snprintf(key, sizeof key, "%s%d", name, i);
            vv_icon(c, a->vec, key, icon, 26, a->dpi, colors[i]);
        }
    }
}

static void view(vv_Ctx *c, void *state) {
    App *a = state;
    const vv_Theme *t = vv_theme();

    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                        .padding = vv_all(28), .gap = 18, .scroll_y = true),
           VV_STYLE(.bg = t->surface_app)) {

        vv_text(c, "craz vector integration",
                VV_STYLE(.fg = t->text_primary, .font_size = 24));
        vv_text(c, "Icons are A8 coverage baked once per size and tinted on the GPU \xe2\x80\x94 "
                   "each row shows one mask drawn in five colours.",
                VV_STYLE(.fg = t->text_muted, .font_size = 13));

        // Tintable icon rows.
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 4, .padding = vv_all(16)),
               VV_STYLE(.bg = t->surface_panel, .radius = vv_r(12),
                        .border_width = vv_all(1), .border_color = t->border_subtle)) {
            for (int i = 0; i < 5; i++)
                icon_row(c, a, a->icons[i].icon, a->icons[i].name);
        }

        // Size sweep of one icon (each size bakes once).
        vv_text(c, "SIZE SWEEP (each size bakes once, then caches)",
                VV_STYLE(.fg = t->text_muted, .font_size = 11));
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 16, .cross = VV_ALIGN_CENTER,
                            .padding = vv_all(16)),
               VV_STYLE(.bg = t->surface_panel, .radius = vv_r(12),
                        .border_width = vv_all(1), .border_color = t->border_subtle)) {
            float sizes[6] = { 16, 22, 30, 40, 54, 72 };
            for (int i = 0; i < 6; i++) {
                char key[16]; snprintf(key, sizeof key, "sw%d", i);
                vv_icon(c, a->vec, key, a->icons[2].icon, sizes[i], a->dpi, t->brand_primary);
            }
        }

        // Full-colour SVG + animated craz canvas, side by side.
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 18, .h = vv_fixed(260)),
               VV_STYLE(.bg = {0})) {
            VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 8,
                                .padding = vv_all(16), .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
                   VV_STYLE(.bg = t->surface_panel, .radius = vv_r(12),
                            .border_width = vv_all(1), .border_color = t->border_subtle)) {
                vv_text(c, "full-colour SVG", VV_STYLE(.fg = t->text_muted, .font_size = 11));
                if (a->svg) {
                    int px = (int)(200 * a->dpi);
                    const vv_ImageRef *r = vv_vector_svg_ref(a->vec, a->svg, px, px);
                    if (r) vv_image(c, "svg", r, vv_fixed(200), vv_fixed(200));
                } else {
                    vv_text(c, "(sample not found)", VV_STYLE(.fg = t->text_muted, .font_size = 12));
                }
            }
            VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 8,
                                .padding = vv_all(16), .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
                   VV_STYLE(.bg = t->surface_panel, .radius = vv_r(12),
                            .border_width = vv_all(1), .border_color = t->border_subtle)) {
                vv_text(c, "dynamic craz canvas (per-frame)", VV_STYLE(.fg = t->text_muted, .font_size = 11));
                const vv_ImageRef *r = vv_canvas_ref(a->canvas);
                if (r) vv_image(c, "canvas", r, vv_fixed(200), vv_fixed(200));
            }
        }
    }
}

int main(void) {
    vv_App *app = vv_app_create("Verve \xc2\xb7 craz icons", 720, 900);
    if (!app) return 1;
    for (const char *const *f = vv_default_font_paths(); *f; f++)
        if (vv_app_load_font(app, *f)) break;

    vv_Ctx ctx;
    vv_init(&ctx);
    vv_set_measure_fn(&ctx, vv_app_measure, app);

    App a = { .app = app, .vec = vv_app_vector(app) };
    a.icons[0] = (NamedIcon){ "Heart", vv_vector_icon_svg(a.vec, SVG_HEART, (int)strlen(SVG_HEART)) };
    a.icons[1] = (NamedIcon){ "Star",  vv_vector_icon_svg(a.vec, SVG_STAR,  (int)strlen(SVG_STAR)) };
    a.icons[2] = (NamedIcon){ "Gear",  vv_vector_icon_svg(a.vec, SVG_GEAR,  (int)strlen(SVG_GEAR)) };
    a.icons[3] = (NamedIcon){ "Check", vv_vector_icon_svg(a.vec, SVG_CHECK, (int)strlen(SVG_CHECK)) };
    a.icons[4] = (NamedIcon){ "Bolt",  vv_vector_icon_svg(a.vec, SVG_BOLT,  (int)strlen(SVG_BOLT)) };
    a.svg = vv_vector_svg_file(a.vec, "../craz/svgs/tiger.svg");

    vv_Input in = {0};
    uint64_t prev = SDL_GetPerformanceCounter();
    while (vv_app_pump(app, &in)) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;
        if (dt > 0.1f) dt = 0.1f;
        a.t += dt;

        int w, h; float dpi;
        vv_app_size(app, &w, &h, &dpi);
        a.dpi = dpi;
        vv_set_window(&ctx, (float)w, (float)h, dpi);

        redraw_canvas(&a, (int)(200 * dpi)); // animated: re-raster each frame

        vv_begin_frame(&ctx, dt, &in);
        view(&ctx, &a);
        vv_CommandBuffer *cmds = vv_end_frame(&ctx);
        vv_Event ev; while (vv_poll_event(&ctx, &ev)) { /* no messages in this demo */ }

        vv_app_frame_begin(app, vv_rgb(0.10f, 0.11f, 0.13f));
        vv_render(vv_app_backend(app), cmds, w, h, dpi);
        vv_app_set_cursor(app, vv_cursor(&ctx));
        vv_app_frame_end(app);
    }

    if (a.svg) vv_vector_svg_free(a.vec, a.svg);
    if (a.canvas) vv_canvas_free(a.canvas);
    vv_shutdown(&ctx);
    vv_app_destroy(app);
    return 0;
}
