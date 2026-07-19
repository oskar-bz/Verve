// host.c — owns the window, GL, App state, and the frame loop; dlopen's the view
// .so and reloads it when the file changes. Because state lives here and only
// the update/view *functions* come from the .so, a reload keeps the counter.
//
//   Terminal 1:  ./build/hotdemo
//   Terminal 2:  edit examples/hot/view.c, then `make hot`  -> window updates
#include "app.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define SO_PATH "build/hotview.so"

typedef struct {
  void       *lib;
  long        mtime;
  vv_UpdateFn update;
  vv_ViewFn   view;
} Hot;

static long so_mtime(void) {
  struct stat st;
  return stat(SO_PATH, &st) == 0 ? (long)st.st_mtime : 0;
}

// dlopen caches by path, so a rebuilt .so at the same path returns the stale
// handle unless its refcount hit zero. Copy to a fresh temp name each reload.
static bool hot_load(Hot *h) {
  static int gen = 0;
  char tmp[64];
  snprintf(tmp, sizeof tmp, "/tmp/vv_hotview_%d.so", gen++);
  char cmd[160];
  snprintf(cmd, sizeof cmd, "cp %s %s", SO_PATH, tmp);
  if (system(cmd) != 0) return false;

  void *lib = dlopen(tmp, RTLD_NOW | RTLD_LOCAL);
  if (!lib) { fprintf(stderr, "[hot] dlopen: %s\n", dlerror()); return false; }
  vv_UpdateFn u = (vv_UpdateFn)dlsym(lib, "view_update");
  vv_ViewFn   v = (vv_ViewFn)dlsym(lib, "view_build");
  if (!u || !v) { fprintf(stderr, "[hot] missing symbols\n"); dlclose(lib); return false; }

  if (h->lib) dlclose(h->lib); // new one loaded first, so a failed build keeps old
  h->lib = lib; h->update = u; h->view = v;
  return true;
}

static bool hot_poll(Hot *h) {
  long m = so_mtime();
  if (m && m != h->mtime) { h->mtime = m; if (hot_load(h)) { printf("[hot] reloaded\n"); return true; } }
  return false;
}

int main(void) {
  vv_App *app = vv_app_create("Verve · Hot Reload", 900, 640);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  Hot hot = {0};
  if (!hot_load(&hot)) { fprintf(stderr, "build %s first (make hot)\n", SO_PATH); return 1; }
  hot.mtime = so_mtime();

  App state = {0};
  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    // Swap in a rebuilt view if it changed; force one rebuild so the new output
    // shows even with no input (otherwise the idle gate would skip it).
    if (hot_poll(&hot)) vv_invalidate(&ctx);

    int w, h; float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, hot.update, hot.view, &state);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.11f, 0.12f, 0.14f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
