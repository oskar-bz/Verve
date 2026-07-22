// finder.c — a fuzzy file finder, and the purest logic/view split in the repo.
// The entire "brain" is fuzzy_score(): a function from (query, string) to an
// int, with no idea a UI exists. update() runs it over the dataset and sorts;
// view() just draws the ranked results. Because each result row is keyed by its
// item id, re-ranking as you type makes rows FLIP-spring to their new
// positions, and rows entering/leaving the top list fade — animation you never
// wrote.
//
//   type            -> results re-rank live
//   up / down       -> move the selection
//   enter           -> "pick" (flashes the selected row)
// Build with `make gui`, run ./build/finder.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NITEMS 2000
#define NSHOW 12 // results rendered (top of the ranking)

enum { MSG_QUERY = 1, MSG_MOVE, MSG_PICK };

typedef struct {
  char items[NITEMS][64];
  char query[64];
  int ranked[NITEMS]; // item indices, best score first
  int score[NITEMS];  // parallel score for the ranked list
  int nranked;
  int sel;    // selection within the shown window
  int picked; // item id last picked (for the flash), -1 none
} Finder;

// ---- the logic layer: a pure scorer, zero UI knowledge ----
// Subsequence match with bonuses for consecutive hits and word boundaries;
// shorter candidates win ties. Returns a score, or INT_MIN for no match.
static int fuzzy_score(const char *q, const char *s) {
  if (!*q)
    return 1; // empty query: everything matches, keep original order
  int score = 0, last = -2, si = 0;
  const char *qc = q;
  for (; s[si] && *qc; si++) {
    if (tolower((unsigned char)s[si]) == tolower((unsigned char)*qc)) {
      score += 10;
      if (last == si - 1)
        score += 15; // consecutive run
      if (si == 0 || s[si - 1] == '/' || s[si - 1] == '_' || s[si - 1] == '.' ||
          s[si - 1] == ' ')
        score += 20; // word boundary
      last = si;
      qc++;
    }
  }
  if (*qc)
    return -1000000;                 // not all query chars consumed
  return score - (int)strlen(s) / 4; // prefer shorter matches
}

static Finder *g_sort;
static int cmp_ranked(const void *a, const void *b) {
  int ia = *(const int *)a, ib = *(const int *)b;
  return g_sort->score[ib] - g_sort->score[ia]; // desc
}

static void rerank(Finder *f) {
  f->nranked = 0;
  for (int i = 0; i < NITEMS; i++) {
    int sc = fuzzy_score(f->query, f->items[i]);
    if (sc > -1000000) {
      f->ranked[f->nranked] = i;
      f->score[i] = sc;
      f->nranked++;
    }
  }
  g_sort = f;
  qsort(f->ranked, (size_t)f->nranked, sizeof(int), cmp_ranked);
  f->sel = 0;
}

static void update(void *st, vv_Event ev) {
  Finder *f = st;
  switch (ev.msg) {
  case MSG_QUERY:
    rerank(f);
    f->picked = -1;
    break;
  case MSG_MOVE: {
    int n = f->nranked < NSHOW ? f->nranked : NSHOW;
    f->sel += (int)ev.data.as_int;
    if (f->sel < 0)
      f->sel = 0;
    if (f->sel >= n)
      f->sel = n ? n - 1 : 0;
    break;
  }
  case MSG_PICK:
    if (f->sel < f->nranked)
      f->picked = f->ranked[f->sel];
    break;
  default:
    break;
  }
}

// ---- the view layer ----
static void view(vv_Ctx *c, void *st) {
  Finder *f = st;
  const vv_Theme *t = vv_theme();
  int n = f->nranked < NSHOW ? f->nranked : NSHOW;

  VV_BOX(c,
         VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                   .padding = vv_all(24), .gap = 14),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {

    // query field
    VV_BOX(c,
           VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER, .main=VV_ALIGN_CENTER,
                     .gap = 10, .padding = vv_hv(14, 4)),
           VV_STYLE(.bg = t->surface, .radius = vv_r(10),
                    .border_width = vv_all(1), .border_color = t->border))
    {
      vv_text(c, ">", VV_STYLE(.fg = t->accent_hi, .font_size = 18));
      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {
        vv_text_field(c, "q", f->query, sizeof f->query,
                      "fuzzy find\xe2\x80\xa6", MSG_QUERY);
      }
      vv_text(c, vv_fmt(c, "%d matches", f->nranked),
              VV_STYLE(.fg = t->text_muted, .font_size = 13));
    }

    // ranked results. Each row keyed by ITEM id, so re-ranking FLIP-slides rows
    // to their new slots and dropped/added rows fade — free, from identity.
    VV_BOX(
        c,
        VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 3),
        VV_STYLE(.bg = {0})) {
      for (int i = 0; i < n; i++) {
        int id = f->ranked[i];
        bool sel = i == f->sel;
        bool pick = id == f->picked;
        char k[12];
        snprintf(k, sizeof k, "i%d", id);
        VV_BOX(c,
               VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                         .cross = VV_ALIGN_CENTER, .gap = 10,
                         .padding = vv_hv(12, 8)),
               VV_STYLE(.bg = pick ? t->accent
                                   : (sel ? t->surface_hi : (vv_Color){0}),
                        .radius = vv_r(8),
                      .hover = &VV_STYLE(.bg=t->surface_hi))) {
          (void)k;
          vv_box_keyed(c, "dot", 3,
                       VV_LAYOUT(.w = vv_fixed(6), .h = vv_fixed(6)),
                       VV_STYLE(.bg = sel ? t->accent_hi : t->text_muted,
                                .radius = vv_r(3)));
          vv_end_box(c);
          vv_text(
              c, f->items[id],
              VV_STYLE(.fg = pick ? t->on_accent : t->text, .font_size = 15));
          VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
          vv_text(c, vv_fmt(c, "%d", f->score[id]),
                  VV_STYLE(.fg = t->text_muted, .font_size = 12));
        }
      }
    }
    vv_text(c, "\xe2\x86\x91\xe2\x86\x93 select \xc2\xb7 enter to pick",
            VV_STYLE(.fg = t->text_muted, .font_size = 12));
  }
}

static void seed(Finder *f) {
  static const char *dirs[] = {
      "src",      "include/verve", ".config/nvim", "docs",       "build",
      "examples", "tests",         "backends",     "vendor/stb", "assets/img"};
  static const char *stems[] = {
      "main",   "config", "vv_layout", "vv_style", "index",  "render", "parser",
      "widget", "theme",  "utils",     "server",   "client", "cache",  "readme",
      "notes",  "todo",   "spring",    "context",  "buffer", "shader"};
  static const char *exts[] = {"c",   "h",   "md",   "toml",
                               "lua", "txt", "glsl", "json"};
  uint32_t rng = 0x51EED;
  for (int i = 0; i < NITEMS; i++) {
    rng = rng * 1664525u + 1013904223u;
    snprintf(f->items[i], sizeof f->items[i], "%s/%s%d.%s",
             dirs[(rng >> 3) % (sizeof dirs / sizeof *dirs)],
             stems[(rng >> 9) % (sizeof stems / sizeof *stems)],
             (int)((rng >> 15) % 90),
             exts[(rng >> 22) % (sizeof exts / sizeof *exts)]);
  }
  f->picked = -1;
  rerank(f);
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Fuzzy Finder", 620, 540);
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

  static Finder f;
  seed(&f);

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  bool up_prev = false, dn_prev = false, ent_prev = false, first = true;
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;
    if (dt > 0.1f)
      dt = 0.1f;

    // Autofocus the query field on the first frame.
    if (first) {
      vv_request_focus_next(&ctx);
      first = false;
    }

    // Selection keys read from the backend (the text field owns typing/left/
    // right; up/down/enter drive selection). Edge-triggered -> messages.
    const bool *ks = SDL_GetKeyboardState(NULL);
    bool up = ks[SDL_SCANCODE_UP], dn = ks[SDL_SCANCODE_DOWN],
         ent = ks[SDL_SCANCODE_RETURN];
    if (up && !up_prev) {
      vv_emit(&ctx, MSG_MOVE, vv_pi(-1));
      vv_invalidate(&ctx);
    }
    if (dn && !dn_prev) {
      vv_emit(&ctx, MSG_MOVE, vv_pi(1));
      vv_invalidate(&ctx);
    }
    if (ent && !ent_prev) {
      vv_emit(&ctx, MSG_PICK, VV_NO_PAYLOAD);
      vv_invalidate(&ctx);
    }
    up_prev = up;
    dn_prev = dn;
    ent_prev = ent;

    int w, h;
    float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &f);
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
