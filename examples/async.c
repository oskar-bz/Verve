// async.c — background work without freezing the UI. A job runs on a worker
// thread (vv_async); the frame loop polls it, animates a progress bar from the
// value the worker reports, and folds the result back into state when it lands.
// Cancel is cooperative — the worker checks vv_async_cancelled and bails.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static const vv_Theme *TH;

enum { MSG_START = 1, MSG_CANCEL };

typedef struct {
  vv_Async *job;
  char      status[128];
  vv_Ctx   *ctx;
} App;

// The worker: a fake ~2s computation that reports progress and can be cancelled.
static void *work(vv_Async *self, void *arg) {
  (void)arg;
  const int N = 100;
  for (int i = 0; i <= N; i++) {
    if (vv_async_cancelled(self)) return NULL; // bail on cancel
    vv_async_set_progress(self, (float)i / (float)N);
    SDL_Delay(20); // 20ms/step — simulate slow work
  }
  char *res = malloc(64);
  snprintf(res, 64, "Computed 100 primes"); // whatever "work" produced
  return res;
}

static void update(void *st, vv_Event ev) {
  App *a = st;
  switch (ev.msg) {
  case MSG_START:
    if (!a->job) { a->job = vv_async_run(work, NULL); snprintf(a->status, sizeof a->status, "Working..."); }
    break;
  case MSG_CANCEL:
    if (a->job) { vv_async_cancel(a->job); snprintf(a->status, sizeof a->status, "Cancelling..."); }
    break;
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  bool running = a->job != NULL;
  float p = running ? vv_async_progress(a->job) : 0.0f;

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 16,
                      .padding = vv_all(30)),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
    vv_text(c, "Background Job", VV_STYLE(.fg = TH->text, .font_size = TH->font_size + 8));

    VV_BOX(c, VV_LAYOUT(.w = vv_fixed(320)), VV_STYLE(.bg = {0})) {
      vv_progress(c, "p", p);
    }
    vv_text(c, vv_fmt(c, "%.0f%%", (double)(p * 100)),
            VV_STYLE(.fg = TH->text_muted, .font_size = TH->font_size));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
      if (running) vv_button(c, "cancel", "Cancel", MSG_CANCEL, VV_NO_PAYLOAD);
      else         vv_button(c, "start", "Start job", MSG_START, VV_NO_PAYLOAD);
    }
    vv_text(c, a->status, VV_STYLE(.fg = TH->text_muted, .font_size = TH->font_size - 1));
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Async", 640, 420);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);
  TH = vv_theme();

  static App state;
  state.ctx = &ctx;
  snprintf(state.status, sizeof state.status, "idle");

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;

    // Poll the job off the UI thread: when it finishes, consume the result and
    // free the handle. While it runs, keep rebuilding so the bar animates.
    if (state.job) {
      void *res;
      if (vv_async_done(state.job, &res)) {
        snprintf(state.status, sizeof state.status, "%s", res ? (char *)res : "Cancelled");
        free(res);
        vv_async_free(state.job);
        state.job = NULL;
      }
      vv_invalidate(&ctx);
    }

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.09f, 0.10f, 0.12f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    } else {
      vv_app_wait_event(app, 16);
    }
  }
  if (state.job) { vv_async_cancel(state.job); vv_async_free(state.job); }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
