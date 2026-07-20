// table.c — a 10,000-row sortable, filterable data table. The whole example is a
// study in the logic/view split: every interesting computation — the filter
// predicate, the sort, the derived list of visible rows, the aggregates — lives
// in the model and runs in update(). view() is a dumb projection: it renders
// whatever `visible[]` currently holds and knows nothing about sorting or
// filtering. Ten thousand rows stay cheap because vv_rows virtualizes the body,
// building only the ~20 rows actually on screen.
//
//   click a header  -> sort by that column (click again to flip)
//   type in filter  -> narrow by name; aggregates + count update live
// Build with `make gui`, run ./build/table.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define NROWS 10000
#define ROWH  30.0f

enum { MSG_SORT = 1, MSG_REFILTER, MSG_RESIZE };
enum { COL_NAME, COL_AMOUNT, COL_STATUS };

typedef struct { char name[28]; int amount; int status; } Row; // status 0 paid,1 due,2 overdue

typedef struct {
  Row  rows[NROWS];      // the full dataset (the model)
  int  visible[NROWS];   // derived: row indices passing the filter, in sort order
  int  nvisible;
  int  sort_col, sort_dir; // dir: +1 asc, -1 desc
  float colw[3];           // column widths (Name grows; Amount/Status resizable)
  char filter[32];
  long sum;              // derived aggregates over the visible set
  double avg;
} Table;

// ---- the logic layer: pure model computation, no UI ----
static Table *g_sort_ctx; // for qsort's comparator

static int cmp_rows(const void *pa, const void *pb) {
  const Row *a = &g_sort_ctx->rows[*(const int *)pa];
  const Row *b = &g_sort_ctx->rows[*(const int *)pb];
  int r = 0;
  switch (g_sort_ctx->sort_col) {
  case COL_NAME:   r = strcmp(a->name, b->name); break;
  case COL_AMOUNT: r = (a->amount > b->amount) - (a->amount < b->amount); break;
  case COL_STATUS: r = a->status - b->status; break;
  }
  return r * g_sort_ctx->sort_dir;
}

static bool ci_contains(const char *hay, const char *needle) {
  if (!*needle) return true;
  for (const char *h = hay; *h; h++) {
    const char *a = h, *b = needle;
    while (*a && *b && (tolower((unsigned char)*a) == tolower((unsigned char)*b))) { a++; b++; }
    if (!*b) return true;
  }
  return false;
}

// Rebuild the derived state from the raw model. This is where all the "app
// logic" lives — filter, sort, aggregate — invoked from update() on any change.
static void recompute(Table *t) {
  t->nvisible = 0;
  t->sum = 0;
  for (int i = 0; i < NROWS; i++)
    if (ci_contains(t->rows[i].name, t->filter)) t->visible[t->nvisible++] = i;

  g_sort_ctx = t;
  qsort(t->visible, (size_t)t->nvisible, sizeof(int), cmp_rows);

  for (int i = 0; i < t->nvisible; i++) t->sum += t->rows[t->visible[i]].amount;
  t->avg = t->nvisible ? (double)t->sum / t->nvisible : 0;
}

static void update(void *st, vv_Event ev) {
  Table *t = st;
  switch (ev.msg) {
  case MSG_SORT: {
    int col = (int)ev.data.as_int;
    if (col == t->sort_col) t->sort_dir = -t->sort_dir;
    else { t->sort_col = col; t->sort_dir = col == COL_AMOUNT ? -1 : 1; }
    recompute(t);
    break;
  }
  case MSG_REFILTER: recompute(t); break; // filter buffer already edited in place
  case MSG_RESIZE: {
    vv_Vec2 v = vv_as_v2(ev.data);         // x = column index, y = new width
    int col = (int)v.x;
    if (col >= 0 && col < 3) t->colw[col] = v.y;
    break;
  }
  default: break;
  }
}

// ---- the view layer: a dumb projection of the model ----
static const char *STATUS[3] = {"paid", "due", "overdue"};

static void cell_amount(vv_Ctx *c, int amount, float w) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.w = vv_fixed(w), .h = vv_grow(1), .main = VV_ALIGN_END,
                      .cross = VV_ALIGN_CENTER),
         VV_STYLE(.bg = {0})) {
    vv_text(c, vv_fmt(c, "%d", amount), VV_STYLE(.fg = t->text, .font_size = 14));
  }
}

static void cell_status(vv_Ctx *c, int status, float w) {
  const vv_Theme *t = vv_theme();
  vv_Color col[3] = {vv_rgb(0.35f, 0.75f, 0.4f), vv_rgb(0.95f, 0.7f, 0.25f), t->danger};
  VV_BOX(c, VV_LAYOUT(.w = vv_fixed(w), .h = vv_grow(1), .cross = VV_ALIGN_CENTER),
         VV_STYLE(.bg = {0})) {
    VV_BOX(c, VV_LAYOUT(.padding = vv_hv(9, 3)),
           VV_STYLE(.bg = vv_rgba(col[status].r, col[status].g, col[status].b, 0.16f),
                    .radius = vv_r(6))) {
      vv_text(c, STATUS[status], VV_STYLE(.fg = col[status], .font_size = 12));
    }
  }
}

// vv_rows callback: render one visible row. `index` is into the derived list.
static void row_fn(vv_Ctx *c, int index, void *ud) {
  Table *t = ud;
  const vv_Theme *th = vv_theme();
  Row *r = &t->rows[t->visible[index]];
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1),
                      .cross = VV_ALIGN_CENTER, .padding = vv_hv(14, 0), .gap = 8),
         VV_STYLE(.bg = index % 2 ? vv_rgba(1, 1, 1, 0.025f) : (vv_Color){0})) {
    VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .cross = VV_ALIGN_CENTER), VV_STYLE(.bg = {0})) {
      vv_text(c, r->name, VV_STYLE(.fg = th->text, .font_size = 14));
    }
    cell_amount(c, r->amount, t->colw[COL_AMOUNT]);
    cell_status(c, r->status, t->colw[COL_STATUS]);
  }
}

static void header_cell(vv_Ctx *c, Table *t, const char *label, int col, bool grow) {
  const vv_Theme *th = vv_theme();
  vv_Style hov = {.bg = th->surface_hi, .set = VV_STYLE_BG};
  char key[8]; snprintf(key, sizeof key, "h%d", col);
  uint32_t h = vv_box_keyed(
      c, key, strlen(key),
      (vv_LayoutDecl){.dir = VV_ROW, .w = grow ? vv_grow(1) : vv_fixed(t->colw[col]),
                      .h = vv_grow(1), .cross = VV_ALIGN_CENTER,
                      .main = col == COL_AMOUNT ? VV_ALIGN_END : VV_ALIGN_START,
                      .padding = vv_hv(14, 0), .gap = 4},
      (vv_Style){.hover = &hov});
  vv_text(c, label, VV_STYLE(.fg = th->text, .font_size = 13));
  if (t->sort_col == col)
    vv_text(c, t->sort_dir > 0 ? "\xe2\x96\xb2" : "\xe2\x96\xbc",
            VV_STYLE(.fg = th->accent_hi, .font_size = 10));

  // Right-edge drag handle: resize a fixed-width column by dragging its border.
  uint32_t grip = 0;
  if (!grow) {
    vv_Style ghov = {.bg = th->accent, .set = VV_STYLE_BG};
    grip = vv_box_keyed(c, "grip", 4,
        (vv_LayoutDecl){.w = vv_fixed(6), .h = vv_grow(1), .has_absolute = true,
                        .absolute = vv_rect(t->colw[col] - 6, 0, 6, 36),
                        .focusable = true, .cursor = VV_CURSOR_RESIZE_H},
        (vv_Style){.hover = &ghov});
    vv_end_box(c);
  }
  vv_end_box(c);

  if (grip && vv_active(c, grip)) {
    float left = vv_node(c, h)->actual_rect.x;
    float neww = vv_clampf(vv_mouse(c).x - left, 70.0f, 400.0f);
    vv_emit(c, MSG_RESIZE, vv_pv2(vv_v2((float)col, neww)));
  } else if (vv_clicked(c, h)) {
    vv_emit(c, MSG_SORT, vv_pi(col));
  }
}

static void view(vv_Ctx *c, void *st) {
  Table *t = st;
  const vv_Theme *th = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(20), .gap = 12),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
    vv_text(c, "Transactions", VV_STYLE(.fg = th->text, .font_size = 22));

    // Header row (aligned to the body columns).
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(36), .gap = 8),
           VV_STYLE(.bg = th->surface, .radius = (vv_Corners){8, 8, 0, 0})) {
      header_cell(c, t, "Name", COL_NAME, true);
      header_cell(c, t, "Amount", COL_AMOUNT, false);
      header_cell(c, t, "Status", COL_STATUS, false);
    }

    // Virtualized body: 10k rows, ~20 built.
    vv_box_keyed(c, "body", 4,
                 VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                           .scroll_y = true, .clip = true),
                 VV_STYLE(.bg = vv_rgb(0.11f, 0.12f, 0.15f)));
    vv_rows(c, t->nvisible, ROWH, row_fn, t);
    vv_end_box(c);

    // Footer: filter field + live aggregates over the visible set.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(44),
                        .cross = VV_ALIGN_CENTER, .gap = 14, .padding = vv_hv(4, 0)),
           VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.w = vv_fixed(240)), VV_STYLE(.bg = {0})) {
        vv_text_field(c, "flt", t->filter, sizeof t->filter, "filter by name\xe2\x80\xa6", MSG_REFILTER);
      }
      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      vv_text(c, vv_fmt(c, "%d rows", t->nvisible),
              VV_STYLE(.fg = th->text_muted, .font_size = 14));
      vv_text(c, vv_fmt(c, "sum %ld", t->sum), VV_STYLE(.fg = th->text, .font_size = 14));
      vv_text(c, vv_fmt(c, "avg %.0f", t->avg), VV_STYLE(.fg = th->text, .font_size = 14));
    }
  }
}

static void seed(Table *t) {
  static const char *N[] = {"Acme", "Globex", "Initech", "Umbrella", "Soylent",
      "Hooli", "Vehement", "Massive", "Stark", "Wayne", "Wonka", "Cyberdyne",
      "Tyrell", "Aperture", "Black Mesa", "Nakatomi", "Gekko", "Pied Piper",
      "Prestige", "Sterling", "Dunder", "Vandelay", "Bluth", "Oscorp"};
  uint32_t rng = 0xC0FFEEu;
  for (int i = 0; i < NROWS; i++) {
    rng = rng * 1664525u + 1013904223u;
    snprintf(t->rows[i].name, sizeof t->rows[i].name, "%s %04d",
             N[(rng >> 8) % (sizeof N / sizeof *N)], i);
    t->rows[i].amount = (int)((rng >> 4) % 100000);
    t->rows[i].status = (int)((rng >> 20) % 3);
  }
  t->sort_col = COL_AMOUNT; t->sort_dir = -1;
  t->colw[COL_NAME] = 0; t->colw[COL_AMOUNT] = 120; t->colw[COL_STATUS] = 110;
  recompute(t);
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Data Table", 720, 620);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  static Table table; // 10k rows: keep it off the stack
  seed(&table);

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &table);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.09f, 0.10f, 0.12f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
