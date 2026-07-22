// sortlab.c — an animated sorting visualiser that shows off Verve's signature
// trick: because keyed nodes keep their identity across rebuilds and every
// node's rect FLIP-springs toward its new layout slot, simply *reordering* the
// bars in the tree makes them glide past one another — the swap animation is
// emergent, not hand-coded. We record a sort as a flat list of compare/swap ops
// and replay them one per tick; the layout engine animates the rest.
//
//   Build:  make sortlab    ->    ./build/sortlab
#include "verve/verve.h"
#include "vv_sdl_gl.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_N   72
#define MAX_OPS 40000

enum { M_ALGO = 1, M_PLAY, M_STEP, M_SHUFFLE, M_COUNT, M_SPEED };

typedef struct { int id, val; } Elem;
typedef struct { int i, j; bool swap; } Op; // compare when !swap

typedef struct {
  Elem  order[MAX_N];   // display order (index = slot, value drives height)
  int   n;
  Op    ops[MAX_OPS];   // recorded program for the current run
  int   nops, pc;       // program + counter
  int   algo;           // 0 bubble 1 insertion 2 selection 3 quick
  bool  playing;
  int   comparisons, swaps;
  int   hi, hj;         // slots currently touched (highlight), -1 = none
  float acc;            // tick accumulator
  float speed;
} App;

static const char *const ALGOS[] = { "Bubble", "Insertion", "Selection", "Quick" };

// ---- op recorder: run the chosen sort on a scratch copy, logging every op ----
static int   g_val[MAX_N];
static Op   *g_ops; static int g_nops;
static int cmp(int i, int j) { g_ops[g_nops++] = (Op){i, j, false}; return g_val[i] - g_val[j]; }
static void swp(int i, int j) {
  g_ops[g_nops++] = (Op){i, j, true};
  int t = g_val[i]; g_val[i] = g_val[j]; g_val[j] = t;
}
static void qsort_rec(int lo, int hi) {
  if (lo >= hi) return;
  int i = lo;
  for (int j = lo; j < hi; j++) if (cmp(j, hi) < 0) { if (i != j) swp(i, j); i++; }
  if (i != hi) swp(i, hi);
  qsort_rec(lo, i - 1); qsort_rec(i + 1, hi);
}
static void record(App *a) {
  for (int i = 0; i < a->n; i++) g_val[i] = a->order[i].val;
  g_ops = a->ops; g_nops = 0;
  switch (a->algo) {
    case 0: // bubble
      for (int i = 0; i < a->n; i++)
        for (int j = 0; j + 1 < a->n - i; j++)
          if (cmp(j, j + 1) > 0) swp(j, j + 1);
      break;
    case 1: // insertion
      for (int i = 1; i < a->n; i++) {
        int j = i;
        while (j > 0 && cmp(j - 1, j) > 0) { swp(j - 1, j); j--; }
      }
      break;
    case 2: // selection
      for (int i = 0; i < a->n; i++) {
        int m = i;
        for (int j = i + 1; j < a->n; j++) if (cmp(j, m) < 0) m = j;
        if (m != i) swp(i, m);
      }
      break;
    case 3: qsort_rec(0, a->n - 1); break;
  }
  a->nops = g_nops; a->pc = 0; a->comparisons = a->swaps = 0; a->hi = a->hj = -1;
}

static void shuffle(App *a) {
  for (int i = 0; i < a->n; i++) a->order[i] = (Elem){ i, i + 1 };
  for (int i = a->n - 1; i > 0; i--) { // Fisher–Yates on values
    int j = rand() % (i + 1);
    int t = a->order[i].val; a->order[i].val = a->order[j].val; a->order[j].val = t;
  }
  record(a);
}

static void step(App *a) {
  if (a->pc >= a->nops) { a->playing = false; a->hi = a->hj = -1; return; }
  Op op = a->ops[a->pc++];
  a->hi = op.i; a->hj = op.j;
  if (op.swap) {
    Elem t = a->order[op.i]; a->order[op.i] = a->order[op.j]; a->order[op.j] = t;
    a->swaps++;
  } else a->comparisons++;
}

static void stat(vv_Ctx *c, const char *label, const char *val) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 2), VV_STYLE(.bg = {0})) {
    vv_text(c, label, VV_STYLE(.fg = t->text_muted, .font_size = 11));
    vv_text(c, val, VV_STYLE(.fg = t->text, .font_size = 20));
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  bool done = a->pc >= a->nops;
  vv_Color lo = vv_rgb(0.30f, 0.62f, 0.95f), hicol = vv_rgb(0.95f, 0.45f, 0.75f);

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .gap = 18, .padding = vv_all(24)),
         VV_STYLE(.bg = vv_rgb(0.07f, 0.08f, 0.11f))) {
    // header
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                        .gap = 16), VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 2), VV_STYLE(.bg = {0})) {
        vv_text(c, "Sort Lab", VV_STYLE(.fg = t->text, .font_size = 26));
        vv_text(c, "bars glide via FLIP springs — the swap animation is free",
                VV_STYLE(.fg = t->text_muted, .font_size = 13));
      }
      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      vv_tabs(c, "algo", ALGOS, 4, a->algo, M_ALGO);
    }

    // the bars
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1), .gap = 3,
                        .cross = VV_ALIGN_END, .padding = vv_all(16)),
           VV_STYLE(.bg = vv_rgb(0.04f, 0.05f, 0.08f), .radius = vv_r(14),
                    .border_width = vv_all(1), .border_color = t->border_subtle)) {
      for (int k = 0; k < a->n; k++) {
        float f = (float)a->order[k].val / (float)a->n;
        bool touched = (k == a->hi || k == a->hj);
        vv_Color col = done ? vv_rgb(0.36f, 0.86f, 0.55f)
                     : touched ? vv_rgb(0.98f, 0.80f, 0.30f)
                               : vv_color_lerp(lo, hicol, f);
        char key[16]; int kl = snprintf(key, sizeof key, "e%d", a->order[k].id);
        vv_box_keyed(c, key, (size_t)kl,
                     VV_LAYOUT(.w = vv_grow(1), .h = vv_percent(f)),
                     VV_STYLE(.bg = col, .radius = vv_r(3)));
        vv_end_box(c);
      }
    }

    // footer: transport + stats
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                        .gap = 22, .padding = (vv_Edges){4, 0, 4, 0}),
           VV_STYLE(.bg = {0})) {
      vv_button(c, "play", done ? "Replay" : a->playing ? "Pause" : "Play", M_PLAY, vv_pi(0));
      vv_button(c, "step", "Step", M_STEP, vv_pi(0));
      vv_button(c, "shuf", "Shuffle", M_SHUFFLE, vv_pi(0));

      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(220), .gap = 4),
             VV_STYLE(.bg = {0})) {
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                            .main = VV_ALIGN_SPACE_BETWEEN), VV_STYLE(.bg = {0})) {
          vv_text(c, "Bars", VV_STYLE(.fg = t->text_muted, .font_size = 12));
          vv_text(c, vv_fmt(c, "%d", a->n), VV_STYLE(.fg = t->text, .font_size = 12));
        }
        vv_slider(c, "count", (float)a->n, 8.0f, (float)MAX_N, M_COUNT);
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                            .main = VV_ALIGN_SPACE_BETWEEN), VV_STYLE(.bg = {0})) {
          vv_text(c, "Speed", VV_STYLE(.fg = t->text_muted, .font_size = 12));
          vv_text(c, vv_fmt(c, "%.1f\xc3\x97", a->speed), VV_STYLE(.fg = t->text, .font_size = 12));
        }
        vv_slider(c, "speed", a->speed, 0.25f, 12.0f, M_SPEED);
      }

      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      stat(c, "COMPARISONS", vv_fmt(c, "%d", a->comparisons));
      stat(c, "SWAPS", vv_fmt(c, "%d", a->swaps));
      stat(c, "PROGRESS", vv_fmt(c, "%d%%", a->nops ? a->pc * 100 / a->nops : 100));
    }
  }
}

static void update(void *st, vv_Event e) {
  App *a = st;
  switch (e.msg) {
    case M_ALGO:    a->algo = (int)e.data.as_int; record(a); a->playing = true; break;
    case M_PLAY:    if (a->pc >= a->nops) record(a); a->playing = !a->playing; break;
    case M_STEP:    a->playing = false; step(a); break;
    case M_SHUFFLE: shuffle(a); a->playing = false; break;
    case M_COUNT:   a->n = (int)(e.data.as_float + 0.5f); shuffle(a); a->playing = false; break;
    case M_SPEED:   a->speed = (float)e.data.as_float; break;
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Sort Lab", 1080, 720);
  if (!app) return 1;
  for (const char *const *f = vv_default_font_paths(); *f; f++)
    if (vv_app_load_font(app, *f)) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);

  App a = { .n = 40, .algo = 0, .speed = 4.0f };
  shuffle(&a);

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    if (a.playing) {
      a.acc += dt * a.speed;
      // up to a few ops per frame at high speed, one per ~55ms base cadence
      int guard = 0;
      while (a.acc >= 0.055f && a.playing && guard++ < 64) { step(&a); a.acc -= 0.055f; }
    }

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_invalidate(&ctx);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &a);
    vv_app_frame_begin(app, vv_rgb(0.07f, 0.08f, 0.11f));
    if (cmds) vv_render(vv_app_backend(app), cmds, w, h, dpi);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    vv_app_frame_end(app);
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
