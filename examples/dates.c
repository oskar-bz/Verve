// dates.c — the evaluation harness for "does one update() handler scale?"
//
// Two instances of a genuinely complex, stateful widget (vv_date_field: a
// calendar with open/close, month navigation, 42 day cells) sit on one screen.
// The question was whether that forces scoped/bubbling message handlers. Look at
// update() below: it is TWO lines. The widget keeps all of its internal churn in
// its own node state and emits exactly one message per instance — so the app's
// dispatch cost is one case per widget, disambiguated by the message constant it
// was handed (MSG_START vs MSG_END). No routing, no bubbling.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>

enum { MSG_START = 1, MSG_END };

typedef struct { int32_t start, end; } Booking;

// The entire application logic. Two complex widgets -> two trivial cases.
static void update(void *st, vv_Event ev) {
  Booking *b = st;
  switch (ev.msg) {
  case MSG_START: b->start = (int32_t)ev.data.as_int; break;
  case MSG_END:   b->end   = (int32_t)ev.data.as_int; break;
  }
}

// Days since 1970-01-01 (Howard Hinnant's civil-to-days), for the duration.
static long days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
static long nights_between(int32_t a, int32_t b) {
  int ay, am, ad, by, bm, bd;
  vv_date_unpack(a, &ay, &am, &ad);
  vv_date_unpack(b, &by, &bm, &bd);
  return days_from_civil(by, bm, bd) - days_from_civil(ay, am, ad);
}

static void field_row(vv_Ctx *c, const char *label, const char *key,
                      int32_t date, vv_Msg change) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 5), VV_STYLE(.bg = {0})) {
    vv_text(c, label, VV_STYLE(.fg = t->text_muted, .font_size = 13));
    vv_date_field(c, key, date, change);
  }
}

static void view(vv_Ctx *c, void *st) {
  Booking *b = st;
  const vv_Theme *t = vv_theme();
  long nights = nights_between(b->start, b->end);

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(360), .gap = 16, .padding = vv_all(24)),
           VV_STYLE(.bg = vv_rgb(0.12f, 0.13f, 0.16f), .radius = vv_r(14),
                    .border_width = vv_all(1), .border_color = t->border)) {
      vv_text(c, "Booking", VV_STYLE(.fg = t->text, .font_size = 22));

      // Two calendar pickers. Each is a lot of widget; each is one message.
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 12), VV_STYLE(.bg = {0})) {
        field_row(c, "Check-in", "start", b->start, MSG_START);
        field_row(c, "Check-out", "end", b->end, MSG_END);
      }

      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .main = VV_ALIGN_SPACE_BETWEEN,
                          .cross = VV_ALIGN_CENTER, .padding = vv_hv(0, 4)),
             VV_STYLE(.bg = {0})) {
        vv_text(c, "Duration", VV_STYLE(.fg = t->text_muted, .font_size = 14));
        vv_text(c, nights > 0 ? vv_fmt(c, "%ld night%s", nights, nights == 1 ? "" : "s")
                              : "select a later check-out",
                VV_STYLE(.fg = nights > 0 ? t->accent_hi : t->danger, .font_size = 16));
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Date Picker", 560, 460);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  Booking state = {.start = vv_date_pack(2026, 7, 19), .end = vv_date_pack(2026, 7, 23)};

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
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
