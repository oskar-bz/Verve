// anim.c — a Verve app that animates. The turn-key runner calls tick() every
// frame with the delta time, so time-based motion needs no manual loop; view()
// is still a pure function of state and just reads the current values. (Inside
// view() you can also call vv_animate() + vv_dt()/vv_clock() to drive motion
// without a tick callback.)
//
//   Build:  make run APP=<name>        (after: make new NAME=<name> KIND=anim)
#include "vv_sdl_gl.h"
#include <math.h>

typedef struct { float t; } App;

static void tick(void *state, float dt) { ((App *)state)->t += dt; }

static void update(void *state, vv_Event ev) { (void)state; (void)ev; }

static void view(vv_Ctx *c, void *state) {
  App *a = state;
  const vv_Theme *t = vv_theme();
  float p = 0.5f + 0.5f * sinf(a->t * 2.0f); // 0..1 pulse

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 20),
         VV_STYLE(.bg = t->surface_app)) {
    vv_text(c, "Animating", VV_STYLE(.fg = t->text_primary, .font_size = 26));
    vv_box_keyed(c, "track", 5, VV_LAYOUT(.w = vv_fixed(360), .h = vv_fixed(18)),
                 VV_STYLE(.bg = t->control_bg_rest, .radius = vv_r(9)));
    {
      vv_box_keyed(c, "fill", 4,
                   VV_LAYOUT(.w = vv_percent(0.1f + 0.9f * p), .h = vv_grow(1)),
                   VV_STYLE(.bg = vv_color_lerp(t->brand_primary, t->status_success, p),
                            .radius = vv_r(9)));
      vv_end_box(c);
    }
    vv_end_box(c);
    vv_text(c, vv_fmt(c, "t = %.1f s", a->t),
            VV_STYLE(.fg = t->text_muted, .font_size = 14));
  }
}

int main(void) {
  App state = {0};
  return vv_app_run(&(vv_AppDesc){
      .title  = "Verve \xc2\xb7 Anim",
      .width  = 560, .height = 360,
      .update = update, .view = view, .state = &state,
      .tick   = tick,   // called every frame with dt → continuous animation
  });
}
