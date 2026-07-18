// gui_demo.c — a live window proving the whole stack: layout + spring
// animation + input, rendered through the SDL3/GL backend.
//
// Move the mouse over the buttons (colors spring in Oklab), click "Add" / "Del"
// to insert/remove rows (every other row FLIP-animates into place; rows fade and
// scale on enter/exit). Scroll the list. Esc quits.
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>

typedef struct { int rows; } Demo;

static const char *FONT_CANDIDATES[] = {
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    NULL,
};

// palette
#define BG      vv_rgb(0.09f, 0.10f, 0.12f)
#define SURFACE vv_rgb(0.14f, 0.15f, 0.18f)
#define ACCENT  vv_rgb(0.22f, 0.55f, 0.95f)
#define ACCENT2 vv_rgb(0.95f, 0.45f, 0.25f)
#define TEXT    vv_rgb(0.92f, 0.93f, 0.95f)
#define MUTED   vv_rgb(0.55f, 0.58f, 0.63f)

// A button whose hover/press styling is declarative (§14.1), so it animates for
// free: base -> hover -> active colors spring through Oklab.
static bool button(vv_Ctx *ctx, const char *key, const char *label, vv_Color base) {
    static vv_Style hov, act;
    hov = (vv_Style){ .bg = vv_color_lerp(base, vv_rgb(1,1,1), 0.15f) };
    act = (vv_Style){ .bg = vv_color_lerp(base, vv_rgb(0,0,0), 0.2f),
                      .transform = vv_scale(0.97f) };
    uint32_t id = vv_box_keyed(ctx, key, 0,
        (vv_LayoutDecl){ .w = vv_fixed(90), .h = vv_fixed(38),
                         .padding = vv_hv(14, 9), .main = VV_ALIGN_CENTER,
                         .cross = VV_ALIGN_CENTER, .focusable = true },
        (vv_Style){ .bg = base, .radius = vv_r(9),
                    .hover = &hov, .active = &act,
                    .shadow = { .color = vv_rgba(0,0,0,0.35f), .offset = vv_v2(0,3), .blur = 10 } });
    vv_text(ctx, label, (vv_Style){ .fg = TEXT, .font_size = 15 });
    vv_end_box(ctx);
    return vv_clicked(ctx, id);
}

static void build(vv_Ctx *ctx, Demo *d) {
    VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                                  .padding = vv_all(24), .gap = 18 }),
           ((vv_Style){ .bg = BG })) {

        vv_text(ctx, "Verve — animation-native UI", (vv_Style){ .fg = TEXT, .font_size = 24 });
        vv_text(ctx, "hover the buttons · add/remove rows · scroll the list",
                (vv_Style){ .fg = MUTED, .font_size = 14 });

        // Toolbar.
        VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 12, .w = vv_grow(1),
                                      .cross = VV_ALIGN_CENTER }),
               ((vv_Style){0})) {
            if (button(ctx, "add", "Add", ACCENT))  d->rows++;
            if (button(ctx, "del", "Del", ACCENT2)) if (d->rows > 0) d->rows--;
            VV_BOX(ctx, ((vv_LayoutDecl){ .w = vv_grow(1) }), (vv_Style){0}) {}
            char buf[32]; snprintf(buf, sizeof buf, "%d rows", d->rows);
            vv_text(ctx, buf, (vv_Style){ .fg = MUTED, .font_size = 14 });
        }

        // Scrollable list. Each row is keyed by index so identity is stable and
        // insert/remove animates (FLIP + enter/exit).
        VV_BOX(ctx, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 8, .w = vv_grow(1),
                                      .h = vv_grow(1), .padding = vv_all(12),
                                      .scroll_y = true, .clip = true }),
               ((vv_Style){ .bg = SURFACE, .radius = vv_r(12) })) {
            for (int i = 0; i < d->rows; i++) {
                char key[16]; snprintf(key, sizeof key, "row%d", i);
                float t = (float)i / 12.0f;
                vv_Color c = vv_color_lerp(ACCENT, ACCENT2, t > 1 ? 1 : t);
                vv_box_keyed(ctx, key, 0,
                    (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_fixed(44),
                                     .padding = vv_hv(14, 0), .cross = VV_ALIGN_CENTER },
                    (vv_Style){ .bg = vv_color_lerp(SURFACE, c, 0.25f),
                                .radius = vv_r(8),
                                .border_width = vv_all(1),
                                .border_color = vv_color_lerp(SURFACE, c, 0.6f) });
                char lbl[32]; snprintf(lbl, sizeof lbl, "Row %d", i + 1);
                vv_text(ctx, lbl, (vv_Style){ .fg = TEXT, .font_size = 15 });
                vv_end_box(ctx);
            }
        }
    }
}

int main(void) {
    vv_App *app = vv_app_create("Verve demo", 720, 560);
    if (!app) return 1;
    vv_FontID font = 0;
    for (int i = 0; FONT_CANDIDATES[i]; i++)
        if ((font = vv_app_load_font(app, FONT_CANDIDATES[i]))) break;

    vv_Ctx ctx; vv_init(&ctx);
    vv_set_measure_fn(&ctx, vv_app_measure, app);

    Demo demo = { .rows = 5 };
    vv_Input in = {0};
    uint64_t prev = SDL_GetPerformanceCounter();

    while (vv_app_pump(app, &in)) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        int w, h; float dpi;
        vv_app_size(app, &w, &h, &dpi);
        vv_set_window(&ctx, (float)w, (float)h, dpi);

        vv_begin_frame(&ctx, dt, &in);
        build(&ctx, &demo);
        vv_CommandBuffer *cmds = vv_end_frame(&ctx);

        vv_app_frame_begin(app, BG);
        vv_render(vv_app_backend(app), cmds, w, h, dpi);
        vv_app_frame_end(app);
    }

    vv_shutdown(&ctx);
    vv_app_destroy(app);
    return 0;
}
