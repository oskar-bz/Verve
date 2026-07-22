// vv_hot.c — implementation of the hot-reload host (see vv_hot.h).
#include "vv_hot.h"

#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
  const char *so_path, *update_sym, *view_sym;
  void       *lib;
  long        mtime;
  vv_UpdateFn update;
  vv_ViewFn   view;
} Hot;

static long so_mtime(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 ? (long)st.st_mtime : 0;
}

// dlopen caches by path, so a rebuilt .so at the same path returns the stale
// handle unless its refcount hit zero. Copy to a fresh temp name each reload so
// we always map the new bytes; the new module loads before the old is closed,
// so a failed build simply keeps the last good view.
static bool hot_load(Hot *h) {
  static unsigned gen = 0;
  char tmp[128];
  snprintf(tmp, sizeof tmp, "/tmp/vv_hot_%d_%u.so", (int)getpid(), gen++);
  char cmd[320];
  snprintf(cmd, sizeof cmd, "cp '%s' '%s'", h->so_path, tmp);
  if (system(cmd) != 0) return false;

  void *lib = dlopen(tmp, RTLD_NOW | RTLD_LOCAL);
  remove(tmp); // the mapping stays valid; the file need not linger
  if (!lib) { fprintf(stderr, "[hot] dlopen: %s\n", dlerror()); return false; }
  vv_UpdateFn u = (vv_UpdateFn)dlsym(lib, h->update_sym);
  vv_ViewFn   v = (vv_ViewFn)dlsym(lib, h->view_sym);
  if (!u || !v) {
    fprintf(stderr, "[hot] missing symbols %s/%s\n", h->update_sym, h->view_sym);
    dlclose(lib);
    return false;
  }
  if (h->lib) dlclose(h->lib);
  h->lib = lib; h->update = u; h->view = v;
  return true;
}

int vv_hot_run(const vv_HotDesc *d) {
  if (!d || !d->so_path || !d->state) return 2;

  Hot hot = {
      .so_path    = d->so_path,
      .update_sym = d->update_sym ? d->update_sym : "view_update",
      .view_sym   = d->view_sym   ? d->view_sym   : "view_build",
  };
  if (!hot_load(&hot)) {
    fprintf(stderr, "[hot] could not load %s (build it first)\n", d->so_path);
    return 1;
  }
  hot.mtime = so_mtime(d->so_path);

  int w = d->width  > 0 ? d->width  : 900;
  int h = d->height > 0 ? d->height : 640;
  vv_App *app = vv_app_create(d->title ? d->title : "Verve \xc2\xb7 Hot Reload", w, h);
  if (!app) return 1;

  const char *const *fonts = d->fonts ? d->fonts : vv_default_font_paths();
  for (int i = 0; fonts[i]; i++)
    if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx;
  vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);
  if (d->clipboard) vv_app_bind_clipboard(app, &ctx);

  vv_Color clear = d->clear.a > 0.0f ? d->clear : vv_rgb(0.11f, 0.12f, 0.14f);
  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;
    if (dt > 0.1f) dt = 0.1f;

    // Reload when the .so changes on disk; force one rebuild so the new output
    // shows even with no input (otherwise idle mode would skip it).
    long m = so_mtime(d->so_path);
    if (m && m != hot.mtime) {
      hot.mtime = m;
      if (hot_load(&hot)) { printf("[hot] reloaded\n"); vv_invalidate(&ctx); }
    }

    int ww, hh; float dpi;
    vv_app_size(app, &ww, &hh, &dpi);
    vv_set_window(&ctx, (float)ww, (float)hh, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, hot.update, hot.view, d->state);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    if (cmds) {
      vv_app_frame_begin(app, clear);
      vv_render(vv_app_backend(app), cmds, ww, hh, dpi);
      vv_app_frame_end(app);
    } else {
      // A tiny timeout (not a full block) so we still poll the .so mtime while
      // idle — otherwise a reload wouldn't show until the next input event.
      vv_app_wait_event(app, 100);
    }
  }
  if (hot.lib) dlclose(hot.lib);
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
