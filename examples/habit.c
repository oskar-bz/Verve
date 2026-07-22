// habit.c — a GitHub-style contribution grid for tracking a daily habit. Click
// a day to cycle its intensity (0..4); the cell springs to its new color
// instead of snapping, and the streak counter reflects the run of days ending
// today.
//
// Why it's a good Verve demo: 371 cells, each just declaring a bg *target* from
// its stored level. Changing one level re-declares that cell's target and the
// per-node color spring glides it there — you write zero animation code, and
// the grid still ripples to life on load because every cell springs up from the
// theme's zero-color. Cell fills interpolate through OKLab (vv_color_lerp), so
// the low->high ramp stays perceptually even.
//
//   click a day     -> cycle 0->1->2->3->4->0, cell springs to the new shade
// Build with `make gui`, run ./build/habit.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>

#define WEEKS 53
#define NDAYS (WEEKS * 7)

enum { MSG_CYCLE = 1 };

typedef struct {
  uint8_t level[NDAYS]; // 0 = empty, 1..4 = intensity
} Habit;

static void update(void *state, vv_Event ev) {
  Habit *h = state;
  if (ev.msg == MSG_CYCLE) {
    int d = (int)ev.data.as_int;
    h->level[d] = (uint8_t)((h->level[d] + 1) % 5);
    printf("SET %d to %d\n", d, h->level[d]);
  }
}

// Empty cells are a flat dark square; filled cells ramp low->high through
// OKLab.
static vv_Color cell_color(int level) {
  if (level <= 0)
    return vv_rgb(0.16f, 0.17f, 0.20f);
  vv_Color lo = vv_rgb(0.13f, 0.32f, 0.20f); // faint green
  vv_Color hi = vv_rgb(0.35f, 0.90f, 0.45f); // vivid green
  return vv_color_lerp(lo, hi, (float)(level - 1) / 3.0f);
}

// Current streak: consecutive filled days counting back from the last cell.
static int streak(const Habit *h) {
  int n = 0;
  for (int d = NDAYS - 1; d >= 0; d--) {
    printf("(%d,%d) ", d, h->level[d]);
    if (h->level[d] == 0) break;
    n++;
  }
  printf("\n");
  return n;
}

static int total(const Habit *h) {
  int n = 0;
  for (int d = 0; d < NDAYS; d++)
    n += h->level[d] > 0;
  return n;
}

static void view(vv_Ctx *c, void *state) {
  Habit *h = state;
  const vv_Theme *t = vv_theme();

  VV_BOX(c,
         VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                   .padding = vv_all(28), .gap = 18),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {

    // Header: title + live streak / total.
    VV_BOX(c,
           VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                     .main = VV_ALIGN_SPACE_BETWEEN),
           VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 2), VV_STYLE(.bg = {0})) {
        vv_text(c, "Meditation", VV_STYLE(.fg = t->text, .font_size = 24));
        vv_text(c, "click a day to log · cycles intensity",
                VV_STYLE(.fg = t->text_muted, .font_size = 13));
      }
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 20, .cross = VV_ALIGN_CENTER),
             VV_STYLE(.bg = {0})) {
        vv_text(c, vv_fmt(c, "%d day streak", streak(h)),
                VV_STYLE(.fg = vv_rgb(0.35f, 0.90f, 0.45f), .font_size = 18));
        vv_text(c, vv_fmt(c, "%d logged", total(h)),
                VV_STYLE(.fg = t->text_muted, .font_size = 15));
      }
    }

    // The grid: a row of week-columns, each 7 day cells top to bottom.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 4, .main = VV_ALIGN_CENTER),
           VV_STYLE(.bg = {0})) {
      for (int w = 0; w < WEEKS; w++) {
        char wk[8];
        snprintf(wk, sizeof wk, "w%d", w);
        vv_box_keyed(c, wk, strlen(wk), VV_LAYOUT(.dir = VV_COLUMN, .gap = 4),
                     VV_STYLE(.bg = {0}));
        for (int d = 0; d < 7; d++) {
          int idx = w * 7 + d;
          char key[10];
          snprintf(key, sizeof key, "d%d", idx);
          vv_Style hov = {.border_color = vv_rgba(1, 1, 1, 0.5f),
                          .set = VV_STYLE_BORDER_COLOR};
          uint32_t id = vv_box_keyed(
              c, key, strlen(key),
              VV_LAYOUT(.w = vv_fixed(15), .h = vv_fixed(15)),
              VV_STYLE(.bg = cell_color(h->level[idx]), .radius = vv_r(3),
                       .border_width = vv_all(1),
                       .border_color = vv_rgba(0, 0, 0, 0), .hover = &hov));
          vv_end_box(c);
          if (vv_clicked(c, id))
            vv_emit(c, MSG_CYCLE, vv_pi(idx));
        }
        vv_end_box(c);
      }
    }

    // Legend.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 6, .cross = VV_ALIGN_CENTER),
           VV_STYLE(.bg = {0})) {
      vv_text(c, "less", VV_STYLE(.fg = t->text_muted, .font_size = 12));
      for (int l = 0; l <= 4; l++) {
        char key[8];
        snprintf(key, sizeof key, "lg%d", l);
        vv_box_keyed(c, key, strlen(key),
                     VV_LAYOUT(.w = vv_fixed(15), .h = vv_fixed(15)),
                     VV_STYLE(.bg = cell_color(l), .radius = vv_r(3)));
        vv_end_box(c);
      }
      vv_text(c, "more", VV_STYLE(.fg = t->text_muted, .font_size = 12));
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Habit", 980, 320);
  if (!app)
    return 1;
  const char *fonts[] = {
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++)
    if (vv_app_load_font(app, fonts[i]))
      break;

  vv_Ctx ctx;
  vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  Habit habit = {0};
  // Seed a plausible-looking history so it isn't blank on open.
  uint32_t rng = 0x1234567u;
  for (int d = 0; d < NDAYS - 3; d++) {
    rng = rng * 1664525u + 1013904223u;
    uint32_t r = (rng >> 24) % 10;
    habit.level[d] = r < 5 ? 0 : (uint8_t)(1 + (r - 5) % 4);
  }

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;
    if (dt > 0.1f)
      dt = 0.1f;

    int w, h;
    float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &habit);
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
