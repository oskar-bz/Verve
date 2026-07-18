// host.c — how little C an embedder writes (sketch; Phase 7).
//
// The C side owns the window, GL, and the frame loop; Lua owns the UI. The
// binding layer (vv_lua_*) wraps the C immediate-mode API into the `ui` table
// the .lua files receive, copying transient Lua strings into the frame arena so
// the core never holds a GC pointer past the frame (§13, §16 — the hazard the
// design already retired). Hot reload = re-run the chunk; `ui.store` keeps state.

#include "verve/verve.h"
#include "verve/vv_lua.h"   // the binding, Phase 7
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>

int main(void) {
  vv_App *app = vv_app_create("Verve · Lua", 900, 640);
  vv_app_load_font(app, "/usr/share/fonts/noto/NotoSans-Regular.ttf");

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  vv_Lua *L = vv_lua_new(&ctx);
  vv_lua_watch(L, "examples/lua/synth.lua");   // reload on file change

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    // vv_lua_frame runs the script's view() through the C build/present tiers,
    // reloading the chunk first if the file changed. Returns NULL when idle.
    vv_CommandBuffer *cmds = vv_lua_frame(L, dt, &in);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.09f, 0.10f, 0.12f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_lua_free(L);
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
