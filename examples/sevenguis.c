// sevenguis.c — the 7GUIs benchmark (https://eugenkiss.github.io/7guis/)
// implemented on Verve: Counter, Temperature, Flight Booker, Timer, CRUD,
// Circle Drawer, Cells. A sidebar switches tasks. Esc-free; close the window.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------ helpers
static const vv_Theme *TH;

static void title(vv_Ctx *c, const char *s) {
    vv_text(c, s, (vv_Style){ .fg = TH->text, .font_size = 22 });
}

// A button with an enabled flag (7GUIs needs disabled Book/Update/etc).
static bool button_en(vv_Ctx *c, const char *key, const char *label, bool enabled) {
    vv_Style hover  = { .bg = TH->accent_hi };
    vv_Style active = { .bg = TH->accent_lo, .transform = vv_scale(0.97f) };
    uint32_t id = vv_box_keyed(c, key, strlen(key),
        (vv_LayoutDecl){ .w = vv_fit(), .h = vv_fixed(36), .padding = vv_hv(16, 8),
                         .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER,
                         .focusable = enabled, .disabled = !enabled },
        (vv_Style){ .bg = enabled ? TH->accent : TH->track, .radius = vv_r(TH->radius),
                    .hover = enabled ? &hover : NULL, .active = enabled ? &active : NULL });
    vv_text(c, label, (vv_Style){ .fg = enabled ? TH->on_accent : TH->text_muted, .font_size = 15 });
    vv_end_box(c);
    return enabled && vv_clicked(c, id);
}

// Fixed-width wrapper around the grow-width text field.
static bool field_w(vv_Ctx *c, const char *key, char *buf, int cap, const char *ph, float w) {
    bool ch;
    vv_box_keyed(c, key, strlen(key), (vv_LayoutDecl){ .w = vv_fixed(w), .h = vv_fixed(34) }, (vv_Style){0});
    {
        char sub[40]; snprintf(sub, sizeof sub, "%s_f", key);
        ch = vv_text_field(c, sub, buf, cap, ph);
    }
    vv_end_box(c);
    return ch;
}

// ------------------------------------------------------------------ 1 Counter
static int g_count;
static void task_counter(vv_Ctx *c) {
    title(c, "Counter");
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 12, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
        char b[32]; snprintf(b, sizeof b, "%d", g_count);
        vv_box_keyed(c, "disp", 4, (vv_LayoutDecl){ .w = vv_fixed(80), .h = vv_fixed(36),
            .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER },
            (vv_Style){ .bg = TH->surface, .radius = vv_r(6) });
        vv_text(c, b, (vv_Style){ .fg = TH->text, .font_size = 16 });
        vv_end_box(c);
        if (vv_button(c, "inc", "Count")) g_count++;
    }
}

// -------------------------------------------------------------- 2 Temperature
static char g_cel[16] = "", g_fah[16] = "";
static void task_temp(vv_Ctx *c) {
    title(c, "Temperature Converter");
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
        if (field_w(c, "cel", g_cel, sizeof g_cel, "0", 90)) {
            char *end; double v = strtod(g_cel, &end);
            if (end != g_cel) snprintf(g_fah, sizeof g_fah, "%.1f", v * 9.0/5.0 + 32.0);
        }
        vv_label(c, "Celsius  =");
        if (field_w(c, "fah", g_fah, sizeof g_fah, "32", 90)) {
            char *end; double v = strtod(g_fah, &end);
            if (end != g_fah) snprintf(g_cel, sizeof g_cel, "%.1f", (v - 32.0) * 5.0/9.0);
        }
        vv_label(c, "Fahrenheit");
    }
}

// ------------------------------------------------------------- 3 Flight Booker
static int  g_flight_type; // 0 one-way, 1 return
static char g_d1[16] = "27.03.2014", g_d2[16] = "27.03.2014";
static bool g_booked;

static bool parse_date(const char *s, int *out) { // days since 0 approx
    int d, m, y;
    if (sscanf(s, "%d.%d.%d", &d, &m, &y) != 3) return false;
    if (d < 1 || d > 31 || m < 1 || m > 12) return false;
    *out = y * 372 + m * 31 + d;
    return true;
}
static void task_flight(vv_Ctx *c) {
    title(c, "Flight Booker");
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 10, .w = vv_fixed(240) }), (vv_Style){0}) {
        // "Dropdown" as a cycle button (real popover is later work).
        const char *types[] = { "one-way flight", "return flight" };
        if (vv_button(c, "type", types[g_flight_type])) g_flight_type ^= 1;

        int t1, t2; bool v1 = parse_date(g_d1, &t1), v2 = parse_date(g_d2, &t2);
        field_w(c, "d1", g_d1, sizeof g_d1, "DD.MM.YYYY", 240);
        // Return date disabled for one-way.
        VV_BOX(c, ((vv_LayoutDecl){ .w = vv_grow(1), .disabled = g_flight_type == 0 }),
               ((vv_Style){ .opacity = g_flight_type == 0 ? 0.4f : 1.0f })) {
            field_w(c, "d2", g_d2, sizeof g_d2, "DD.MM.YYYY", 240);
        }

        bool ok = v1 && (g_flight_type == 0 || (v2 && t2 >= t1));
        if (button_en(c, "book", "Book", ok)) g_booked = true;
        if (g_booked) {
            char msg[96];
            if (g_flight_type == 0) snprintf(msg, sizeof msg, "Booked one-way for %s.", g_d1);
            else snprintf(msg, sizeof msg, "Booked %s → %s.", g_d1, g_d2);
            vv_text(c, msg, (vv_Style){ .fg = TH->accent_hi, .font_size = 14 });
        }
    }
}

// -------------------------------------------------------------------- 4 Timer
static float g_elapsed, g_duration = 12.0f;
static void task_timer(vv_Ctx *c, float dt) {
    if (g_elapsed < g_duration) g_elapsed = fminf(g_elapsed + dt, g_duration);
    title(c, "Timer");
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 14, .w = vv_fixed(320) }), (vv_Style){0}) {
        float frac = g_duration > 0 ? g_elapsed / g_duration : 0;
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
            vv_label(c, "Elapsed:");
            // Gauge: fill width is continuously driven -> VV_INSTANT_RECT (§6.4.1).
            vv_box_keyed(c, "gauge", 5, (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_fixed(18) },
                         (vv_Style){ .bg = TH->track, .radius = vv_r(9) });
            {
                vv_box_keyed(c, "fill", 1, (vv_LayoutDecl){ .has_absolute = true,
                    .absolute = vv_rect(0, 0, frac * 220.0f, 18) },
                    (vv_Style){ .bg = TH->accent, .radius = vv_r(9), .transition_mask = VV_INSTANT_RECT });
                vv_end_box(c);
            }
            vv_end_box(c);
        }
        char b[32]; snprintf(b, sizeof b, "%.1f s", (double)g_elapsed);
        vv_label(c, b);
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
            vv_label(c, "Duration:");
            g_duration = vv_slider(c, "dur", g_duration, 1, 30);
        }
        if (vv_button(c, "reset", "Reset")) g_elapsed = 0;
    }
}

// --------------------------------------------------------------------- 5 CRUD
static struct { char name[24], surname[24]; } g_db[128];
static int  g_db_n = 0, g_sel = -1;
static char g_filter[24] = "", g_nm[24] = "", g_sn[24] = "";
static bool g_crud_init;

static void task_crud(vv_Ctx *c) {
    if (!g_crud_init) {
        strcpy(g_db[0].name, "Hans"); strcpy(g_db[0].surname, "Emil");
        strcpy(g_db[1].name, "Max");  strcpy(g_db[1].surname, "Mustermann");
        strcpy(g_db[2].name, "Roman"); strcpy(g_db[2].surname, "Tisch");
        g_db_n = 3; g_crud_init = true;
    }
    title(c, "CRUD");
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 10, .w = vv_grow(1), .h = vv_grow(1) }), (vv_Style){0}) {
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
            vv_label(c, "Filter:");
            field_w(c, "flt", g_filter, sizeof g_filter, "surname prefix", 180);
        }
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 14, .w = vv_grow(1), .h = vv_grow(1) }), (vv_Style){0}) {
            // List.
            VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 4, .w = vv_fixed(240),
                .h = vv_grow(1), .padding = vv_all(6), .scroll_y = true, .clip = true }),
                ((vv_Style){ .bg = TH->surface, .radius = vv_r(8) })) {
                for (int i = 0; i < g_db_n; i++) {
                    if (g_filter[0] && strncmp(g_db[i].surname, g_filter, strlen(g_filter)) != 0) continue;
                    char lbl[56], key[16];
                    snprintf(lbl, sizeof lbl, "%s, %s", g_db[i].surname, g_db[i].name);
                    snprintf(key, sizeof key, "it%d", i);
                    if (vv_list_item(c, key, lbl, g_sel == i)) {
                        g_sel = i;
                        strcpy(g_nm, g_db[i].name); strcpy(g_sn, g_db[i].surname);
                    }
                }
            }
            // Editor.
            VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 8, .w = vv_fixed(220) }), (vv_Style){0}) {
                VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
                    vv_label(c, "Name:"); field_w(c, "nm", g_nm, sizeof g_nm, "", 150);
                }
                VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
                    vv_label(c, "Surname:"); field_w(c, "sn", g_sn, sizeof g_sn, "", 130);
                }
            }
        }
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 8 }), (vv_Style){0}) {
            if (button_en(c, "create", "Create", g_nm[0] || g_sn[0]) && g_db_n < 128) {
                strcpy(g_db[g_db_n].name, g_nm); strcpy(g_db[g_db_n].surname, g_sn); g_db_n++;
            }
            if (button_en(c, "update", "Update", g_sel >= 0)) {
                strcpy(g_db[g_sel].name, g_nm); strcpy(g_db[g_sel].surname, g_sn);
            }
            if (button_en(c, "delete", "Delete", g_sel >= 0)) {
                for (int i = g_sel; i < g_db_n - 1; i++) g_db[i] = g_db[i + 1];
                g_db_n--; g_sel = -1; g_nm[0] = g_sn[0] = 0;
            }
        }
    }
}

// ------------------------------------------------------------- 6 Circle Drawer
typedef struct { float x, y, r; } Circle;
typedef struct { Circle c[64]; int n; } CircleSet;
static CircleSet g_hist[128];
static int  g_hist_n = 1, g_hist_i = 0;   // undo stack
static int  g_csel = -1;

static CircleSet *cur(void) { return &g_hist[g_hist_i]; }
static void push_hist(void) {
    if (g_hist_i + 1 >= 128) return;
    g_hist[g_hist_i + 1] = g_hist[g_hist_i];
    g_hist_i++; g_hist_n = g_hist_i + 1;
}
static void task_circle(vv_Ctx *c) {
    title(c, "Circle Drawer");
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 10, .w = vv_grow(1), .h = vv_grow(1) }), (vv_Style){0}) {
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 8, .main = VV_ALIGN_CENTER }), (vv_Style){0}) {
            if (button_en(c, "undo", "Undo", g_hist_i > 0)) { g_hist_i--; g_csel = -1; }
            if (button_en(c, "redo", "Redo", g_hist_i < g_hist_n - 1)) { g_hist_i++; g_csel = -1; }
        }
        uint32_t canvas = vv_box_keyed(c, "canvas", 6,
            (vv_LayoutDecl){ .w = vv_grow(1), .h = vv_grow(1), .clip = true },
            (vv_Style){ .bg = TH->surface, .radius = vv_r(8) });
        {
            CircleSet *cs = cur();
            vv_Rect cr = vv_node(c, canvas)->actual_rect;
            vv_Vec2 m = vv_mouse(c);
            // Selection follows the nearest circle under the pointer.
            for (int i = 0; i < cs->n; i++) {
                Circle *ci = &cs->c[i];
                bool sel = (i == g_csel);
                vv_box_keyed(c, "c", (uint32_t)(100 + i),
                    (vv_LayoutDecl){ .has_absolute = true,
                        .absolute = vv_rect(ci->x - ci->r, ci->y - ci->r, ci->r * 2, ci->r * 2) },
                    (vv_Style){ .bg = sel ? TH->accent_lo : vv_rgba(1,1,1,0.06f),
                                .radius = vv_r(ci->r), .border_width = vv_all(2),
                                .border_color = TH->text });
                vv_end_box(c);
            }
            // Click on empty canvas adds a circle; click near one selects it.
            if (vv_clicked(c, canvas)) {
                float lx = m.x - cr.x, ly = m.y - cr.y;
                int hit = -1; float best = 1e9f;
                for (int i = 0; i < cs->n; i++) {
                    float dx = cs->c[i].x - lx, dy = cs->c[i].y - ly;
                    float d = sqrtf(dx*dx + dy*dy);
                    if (d < cs->c[i].r && d < best) { best = d; hit = i; }
                }
                if (hit >= 0) g_csel = hit;
                else if (cs->n < 64) {
                    push_hist(); CircleSet *ns = cur();
                    ns->c[ns->n++] = (Circle){ lx, ly, 26 };
                    g_csel = ns->n - 1;
                }
            }
        }
        vv_end_box(c);
        if (g_csel >= 0 && g_csel < cur()->n) {
            VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER }), (vv_Style){0}) {
                vv_label(c, "Diameter:");
                float d = cur()->c[g_csel].r * 2;
                float nd = vv_slider(c, "diam", d, 8, 160);
                if (fabsf(nd - d) > 0.01f) cur()->c[g_csel].r = nd * 0.5f;
            }
        }
    }
}

// -------------------------------------------------------------------- 7 Cells
#define CELL_ROWS 12
#define CELL_COLS 6
static char g_cells[CELL_ROWS][CELL_COLS][32];
static int  g_edit_r = -1, g_edit_c = -1;
static bool g_edit_fresh;

static double eval_cell(int r, int c, int depth);
static double eval_expr(const char *s, int depth) {
    // Very small evaluator: sum/diff of terms, terms are number or A1 ref.
    double acc = 0; int sign = 1; const char *p = s;
    while (*p) {
        while (*p == ' ') p++;
        double term = 0;
        if (*p >= 'A' && *p <= 'Z') {
            int cc = *p - 'A'; p++;
            int rr = 0; while (*p >= '0' && *p <= '9') rr = rr*10 + (*p++ - '0');
            rr -= 1;
            if (rr >= 0 && rr < CELL_ROWS && cc >= 0 && cc < CELL_COLS)
                term = eval_cell(rr, cc, depth + 1);
        } else {
            char *end; term = strtod(p, &end); p = end;
        }
        acc += sign * term;
        while (*p == ' ') p++;
        if (*p == '+') { sign = 1; p++; }
        else if (*p == '-') { sign = -1; p++; }
        else break;
    }
    return acc;
}
static double eval_cell(int r, int c, int depth) {
    if (depth > 32) return 0; // cycle guard
    const char *s = g_cells[r][c];
    if (s[0] == '=') return eval_expr(s + 1, depth);
    char *end; double v = strtod(s, &end);
    return end != s ? v : 0;
}

static void task_cells(vv_Ctx *c) {
    title(c, "Cells");
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 0, .w = vv_grow(1), .h = vv_grow(1),
        .scroll_y = true, .clip = true }), ((vv_Style){ .bg = TH->surface, .radius = vv_r(8) })) {
        // Header row.
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 1, .padding = vv_hv(1, 1) }), (vv_Style){0}) {
            vv_box_keyed(c, "corner", 1, (vv_LayoutDecl){ .w = vv_fixed(36), .h = vv_fixed(26) }, (vv_Style){0});
            vv_end_box(c);
            for (int col = 0; col < CELL_COLS; col++) {
                char h[4] = { (char)('A' + col), 0 };
                char k[8]; snprintf(k, sizeof k, "h%d", col);
                vv_box_keyed(c, k, 0, (vv_LayoutDecl){ .w = vv_fixed(88), .h = vv_fixed(26),
                    .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER }, (vv_Style){ .bg = TH->surface_hi });
                vv_text(c, h, (vv_Style){ .fg = TH->text_muted, .font_size = 13 });
                vv_end_box(c);
            }
        }
        for (int r = 0; r < CELL_ROWS; r++) {
            char rk[8]; snprintf(rk, sizeof rk, "r%d", r);
            VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .gap = 1, .padding = vv_hv(1, 0) }), (vv_Style){0}) {
                char rl[4]; snprintf(rl, sizeof rl, "%d", r + 1);
                vv_box_keyed(c, rk, 0, (vv_LayoutDecl){ .w = vv_fixed(36), .h = vv_fixed(34),
                    .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER }, (vv_Style){ .bg = TH->surface_hi });
                vv_text(c, rl, (vv_Style){ .fg = TH->text_muted, .font_size = 13 });
                vv_end_box(c);
                for (int col = 0; col < CELL_COLS; col++) {
                    char ck[12]; snprintf(ck, sizeof ck, "c%d_%d", r, col);
                    bool editing = (g_edit_r == r && g_edit_c == col);
                    // While editing, the text field supplies its own chrome, so
                    // the cell drops its border to avoid a doubled outline.
                    uint32_t cell = vv_box_keyed(c, ck, strlen(ck),
                        (vv_LayoutDecl){ .w = vv_fixed(88), .h = vv_fixed(34),
                            .padding = editing ? vv_all(0) : vv_hv(6, 0),
                            .cross = VV_ALIGN_CENTER, .focusable = true, .clip = true },
                        (vv_Style){ .bg = TH->surface,
                                    .border_width = editing ? vv_all(0) : vv_all(1),
                                    .border_color = TH->border });
                    {
                        if (editing) {
                            if (g_edit_fresh) { vv_request_focus_next(c); g_edit_fresh = false; }
                            char fk[16]; snprintf(fk, sizeof fk, "e%d_%d", r, col);
                            vv_text_field(c, fk, g_cells[r][col], 32, "");
                        } else {
                            char disp[40];
                            if (g_cells[r][col][0] == '=')
                                snprintf(disp, sizeof disp, "%.4g", eval_cell(r, col, 0));
                            else
                                snprintf(disp, sizeof disp, "%s", g_cells[r][col]);
                            vv_text(c, disp, (vv_Style){ .fg = TH->text, .font_size = 13 });
                        }
                    }
                    vv_end_box(c);
                    if (vv_clicked(c, cell)) { g_edit_r = r; g_edit_c = col; g_edit_fresh = true; }
                }
            }
        }
    }
}

// -------------------------------------------------------------------- shell
static const char *TASKS[] = {
    "Counter", "Temperature", "Flight Booker", "Timer", "CRUD", "Circle Drawer", "Cells"
};
static int g_task = 0;

static void build(vv_Ctx *c, float dt) {
    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1) }),
           ((vv_Style){ .bg = vv_rgb(0.09f, 0.10f, 0.12f) })) {
        // Sidebar.
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 4, .w = vv_fixed(170), .h = vv_grow(1),
            .padding = vv_all(12) }), ((vv_Style){ .bg = vv_rgb(0.12f, 0.13f, 0.16f) })) {
            vv_text(c, "7GUIs", (vv_Style){ .fg = TH->text, .font_size = 20 });
            vv_box_keyed(c, "sp", 1, (vv_LayoutDecl){ .h = vv_fixed(8) }, (vv_Style){0}); vv_end_box(c);
            for (int i = 0; i < 7; i++) {
                char k[8]; snprintf(k, sizeof k, "t%d", i);
                if (vv_list_item(c, k, TASKS[i], g_task == i)) g_task = i;
            }
        }
        // Content.
        VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 18, .w = vv_grow(1), .h = vv_grow(1),
            .padding = vv_all(28) }), (vv_Style){0}) {
            switch (g_task) {
                case 0: task_counter(c); break;
                case 1: task_temp(c); break;
                case 2: task_flight(c); break;
                case 3: task_timer(c, dt); break;
                case 4: task_crud(c); break;
                case 5: task_circle(c); break;
                case 6: task_cells(c); break;
            }
        }
    }
}

int main(void) {
    vv_App *app = vv_app_create("Verve · 7GUIs", 900, 640);
    if (!app) return 1;
    const char *fonts[] = { "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL };
    for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

    vv_Ctx ctx; vv_init(&ctx);
    vv_set_measure_fn(&ctx, vv_app_measure, app);
    TH = vv_theme();

    vv_Input in = {0};
    uint64_t prev = SDL_GetPerformanceCounter();
    while (vv_app_pump(app, &in)) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now; if (dt > 0.1f) dt = 0.1f;

        int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
        vv_set_window(&ctx, (float)w, (float)h, dpi);

        vv_begin_frame(&ctx, dt, &in);
        build(&ctx, dt);
        vv_CommandBuffer *cmds = vv_end_frame(&ctx);

        vv_app_frame_begin(app, vv_rgb(0.09f, 0.10f, 0.12f));
        vv_render(vv_app_backend(app), cmds, w, h, dpi);
        vv_app_frame_end(app);
    }
    vv_shutdown(&ctx);
    vv_app_destroy(app);
    return 0;
}
