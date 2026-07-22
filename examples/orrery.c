// orrery.c — a living orrery: a stylised solar system animated in real time with
// glowing bodies, elliptical orbit rings, and fading motion trails, all drawn as
// freeform vector geometry (vv_draw_*) that the core lowers into the GL backend's
// polygon path. A side panel of live controls (time-warp, body count, ring/trail
// toggles) drives it — the classic "custom canvas + reactive UI" combination.
//
//   Build:  make orrery    ->    ./build/orrery
#include "verve/verve.h"
#include "verve/vv_draw.h"
#include "vv_sdl_gl.h"

#include <math.h>
#include <stdlib.h>

#define CANVAS   680.0f     // fixed logical size of the drawing area
#define NBODY    8
#define NSTAR    140
#define TRAIL    64         // trail history length per body
#define TILT     0.44f      // vertical squash → tilted-plane look

enum { M_SPEED = 1, M_COUNT, M_RINGS, M_TRAILS, M_GLOW, M_PLAY, M_RESET };

typedef struct { float r, w, size, phase; vv_Color col; float moon_r, moon_w; } Body;

typedef struct {
  float   t;              // simulation time
  float   speed;          // time-warp multiplier
  int     count;          // visible bodies
  bool    rings, trails, glow, playing;
  vv_Vec2 star[NSTAR];    // static starfield (canvas-local)
  vv_Vec2 trail[NBODY][TRAIL];
  int     head, filled;
} App;

// A fixed, pleasing set of bodies (stylised, not to scale).
static const Body BODIES[NBODY] = {
  { 70,  1.60f, 5,  0.0f, {0.62f,0.66f,0.72f,1}, 0, 0 },      // mercury-ish
  { 108, 1.18f, 8,  1.1f, {0.92f,0.78f,0.45f,1}, 0, 0 },      // venus
  { 150, 0.95f, 9,  2.4f, {0.38f,0.66f,0.95f,1}, 16, 6.0f },  // earth + moon
  { 196, 0.78f, 7,  3.9f, {0.90f,0.48f,0.34f,1}, 0, 0 },      // mars
  { 258, 0.52f, 16, 0.6f, {0.86f,0.72f,0.52f,1}, 34, 3.4f },  // jupiter + moon
  { 316, 0.41f, 14, 4.7f, {0.85f,0.79f,0.60f,1}, 0, 0 },      // saturn
  { 366, 0.31f, 11, 2.0f, {0.55f,0.82f,0.86f,1}, 0, 0 },      // uranus
  { 410, 0.24f, 11, 5.5f, {0.40f,0.55f,0.95f,1}, 0, 0 },      // neptune
};

static void seed_stars(App *a) {
  for (int i = 0; i < NSTAR; i++)
    a->star[i] = vv_v2((float)(rand() % (int)CANVAS), (float)(rand() % (int)CANVAS));
}

static void reset(App *a) {
  a->t = 0; a->head = 0; a->filled = 0;
  memset(a->trail, 0, sizeof a->trail);
}

// Body position at the current sim time (canvas-local coords).
static vv_Vec2 body_pos(const App *a, int i) {
  const Body *b = &BODIES[i];
  float th = b->phase + a->t * b->w;
  return vv_v2(CANVAS * 0.5f + b->r * cosf(th),
               CANVAS * 0.5f + b->r * TILT * sinf(th));
}

// Append n ellipse points (closed) into `out`; returns n.
static int ellipse(vv_Vec2 *out, int n, float cx, float cy, float rx, float ry) {
  for (int i = 0; i < n; i++) {
    float th = (float)i / (float)(n - 1) * 6.2831853f;
    out[i] = vv_v2(cx + rx * cosf(th), cy + ry * sinf(th));
  }
  return n;
}

static void draw_scene(vv_Ctx *c, uint32_t cv, App *a) {
  const vv_Theme *t = vv_theme();
  float cx = CANVAS * 0.5f, cy = CANVAS * 0.5f;

  // starfield
  vv_draw_points(c, cv, a->star, NSTAR, 1.0f, vv_rgba(1, 1, 1, 0.35f));

  // orbit rings
  if (a->rings) {
    vv_Vec2 ring[65];
    for (int i = 0; i < a->count; i++) {
      int n = ellipse(ring, 65, cx, cy, BODIES[i].r, BODIES[i].r * TILT);
      vv_draw_polyline(c, cv, ring, n, 1.0f, vv_rgba(0.5f, 0.6f, 0.8f, 0.16f));
    }
  }

  // central star glow: concentric discs, faint→bright
  if (a->glow) {
    vv_Vec2 ctr = vv_v2(cx, cy);
    float gr[4] = {46, 34, 24, 15};
    float ga[4] = {0.08f, 0.13f, 0.22f, 0.9f};
    for (int g = 0; g < 4; g++)
      vv_draw_points(c, cv, &ctr, 1, gr[g], vv_rgba(1.0f, 0.86f, 0.45f, ga[g]));
  } else {
    vv_Vec2 ctr = vv_v2(cx, cy);
    vv_draw_points(c, cv, &ctr, 1, 15, vv_rgb(1.0f, 0.86f, 0.45f));
  }

  // trails (one fading polyline per body)
  if (a->trails && a->filled > 1) {
    for (int i = 0; i < a->count; i++) {
      vv_Vec2 pts[TRAIL]; int n = 0;
      for (int k = 0; k < a->filled; k++) {
        int idx = (a->head - a->filled + k + TRAIL * 4) % TRAIL;
        pts[n++] = a->trail[i][idx];
      }
      vv_Color tc = BODIES[i].col; tc.a = 0.28f;
      vv_draw_polyline(c, cv, pts, n, 1.5f, tc);
    }
  }

  // planets (+ moons)
  for (int i = 0; i < a->count; i++) {
    vv_Vec2 p = body_pos(a, i);
    vv_draw_points(c, cv, &p, 1, BODIES[i].size + 1.5f,
                   vv_rgba(BODIES[i].col.r, BODIES[i].col.g, BODIES[i].col.b, 0.30f));
    vv_draw_points(c, cv, &p, 1, BODIES[i].size, BODIES[i].col);
    if (BODIES[i].moon_r > 0) {
      float mth = a->t * BODIES[i].moon_w;
      vv_Vec2 m = vv_v2(p.x + BODIES[i].moon_r * cosf(mth),
                        p.y + BODIES[i].moon_r * TILT * sinf(mth));
      vv_draw_points(c, cv, &m, 1, 3.0f, vv_rgba(0.85f, 0.87f, 0.92f, 1));
    }
  }
  (void)t;
}

// ---- side panel ------------------------------------------------------------
static void row_toggle(vv_Ctx *c, const char *label, const char *key, bool v, vv_Msg m) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                      .main = VV_ALIGN_SPACE_BETWEEN), VV_STYLE(.bg = {0})) {
    vv_text(c, label, VV_STYLE(.fg = t->text, .font_size = 14));
    vv_toggle(c, key, v, m);
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1), .gap = 0),
         VV_STYLE(.bg = vv_rgb(0.03f, 0.04f, 0.07f))) {
    // ---- stage (centered canvas) ----
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                        .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
           VV_STYLE(.bg = {0})) {
      uint32_t cv = vv_box_keyed(c, "canvas", 6,
                                 VV_LAYOUT(.w = vv_fixed(CANVAS), .h = vv_fixed(CANVAS)),
                                 VV_STYLE(.bg = vv_rgb(0.02f, 0.03f, 0.05f),
                                          .radius = vv_r(16)));
      draw_scene(c, cv, a);
      vv_end_box(c);
    }

    // ---- control panel ----
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(300), .h = vv_grow(1),
                        .gap = 18, .padding = vv_all(22)),
           VV_STYLE(.bg = t->surface_panel,
                    .border_width = (vv_Edges){0, 0, 0, 1}, .border_color = t->border)) {
      vv_text(c, "Orrery", VV_STYLE(.fg = t->text, .font_size = 26));
      vv_text(c, "a stylised solar system,\nanimated with vector primitives",
              VV_STYLE(.fg = t->text_muted, .font_size = 13));

      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 12,
                          .padding = vv_all(16)),
             VV_STYLE(.bg = t->surface_card, .radius = vv_r(12),
                      .border_width = vv_all(1), .border_color = t->border_subtle)) {
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                            .main = VV_ALIGN_SPACE_BETWEEN), VV_STYLE(.bg = {0})) {
          vv_text(c, "Time warp", VV_STYLE(.fg = t->text, .font_size = 14));
          vv_text(c, vv_fmt(c, "%.1f\xc3\x97", a->speed),
                  VV_STYLE(.fg = t->accent, .font_size = 14));
        }
        vv_slider(c, "speed", a->speed, 0.0f, 6.0f, M_SPEED);

        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                            .main = VV_ALIGN_SPACE_BETWEEN), VV_STYLE(.bg = {0})) {
          vv_text(c, "Bodies", VV_STYLE(.fg = t->text, .font_size = 14));
          vv_text(c, vv_fmt(c, "%d", a->count),
                  VV_STYLE(.fg = t->accent, .font_size = 14));
        }
        vv_slider(c, "count", (float)a->count, 1.0f, (float)NBODY, M_COUNT);
      }

      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 12,
                          .padding = vv_all(16)),
             VV_STYLE(.bg = t->surface_card, .radius = vv_r(12),
                      .border_width = vv_all(1), .border_color = t->border_subtle)) {
        row_toggle(c, "Orbit rings", "tr", a->rings, M_RINGS);
        row_toggle(c, "Motion trails", "tt", a->trails, M_TRAILS);
        row_toggle(c, "Star glow", "tg", a->glow, M_GLOW);
      }

      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 10), VV_STYLE(.bg = {0})) {
        vv_button(c, "play", a->playing ? "Pause" : "Play", M_PLAY, vv_pi(0));
        vv_button(c, "reset", "Reset", M_RESET, vv_pi(0));
      }

      VV_BOX(c, VV_LAYOUT(.h = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      vv_text(c, vv_fmt(c, "epoch  %.1f", a->t),
              VV_STYLE(.fg = t->text_muted, .font_size = 12));
    }
  }
}

static void update(void *st, vv_Event e) {
  App *a = st;
  switch (e.msg) {
    case M_SPEED:  a->speed = (float)e.data.as_float; break;
    case M_COUNT:  a->count = (int)(e.data.as_float + 0.5f); break;
    case M_RINGS:  a->rings = e.data.as_int; break;
    case M_TRAILS: a->trails = e.data.as_int; break;
    case M_GLOW:   a->glow = e.data.as_int; break;
    case M_PLAY:   a->playing = !a->playing; break;
    case M_RESET:  reset(a); break;
  }
}

// Advance the simulation each frame — the turn-key runner's animation hook, so
// the whole app is just tick + view + update with no manual loop.
static void tick(void *st, float dt) {
  App *a = st;
  if (!a->playing) return;
  a->t += dt * a->speed;
  for (int i = 0; i < NBODY; i++) a->trail[i][a->head] = body_pos(a, i);
  a->head = (a->head + 1) % TRAIL;
  if (a->filled < TRAIL) a->filled++;
}

int main(void) {
  App a = { .speed = 1.4f, .count = NBODY, .rings = true, .trails = true,
            .glow = true, .playing = true };
  seed_stars(&a);
  return vv_app_run(&(vv_AppDesc){
      .title = "Verve \xc2\xb7 Orrery", .width = 1160, .height = 760,
      .clear = vv_rgb(0.03f, 0.04f, 0.07f),
      .update = update, .view = view, .state = &a,
      .tick = tick,        // #1: continuous animation, no manual loop
      .devtools = true,    // #4: F12 inspector, F11 perf HUD
  });
}
