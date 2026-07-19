// i18n.c — the Path-A i18n MVP in action: a dynamic glyph atlas rasterizes any
// codepoint on demand, with a font-fallback chain across scripts. Load Noto Sans
// (Latin/Greek/Cyrillic/symbols) first, then Noto Sans CJK for the ideographs;
// the backend picks whichever font has each glyph. Also shows a multi-line
// editor you can type non-ASCII into (Ctrl-C/V/X work), and CJK line wrapping.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>

enum { MSG_EDIT = 1 };
typedef struct { char text[512]; } App;

static void update(void *st, vv_Event ev) { (void)st; (void)ev; }

static void line(vv_Ctx *c, const char *label, const char *text) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 12, .cross = VV_ALIGN_CENTER),
         VV_STYLE(.bg = {0})) {
    VV_BOX(c, VV_LAYOUT(.w = vv_fixed(90)), VV_STYLE(.bg = {0}))
      vv_text(c, label, VV_STYLE(.fg = t->text_muted, .font_size = 13));
    vv_text(c, text, VV_STYLE(.fg = t->text, .font_size = 19));
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(24), .gap = 12),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
    vv_text(c, "Unicode \xe2\x80\xa2 dynamic atlas + font fallback",
            VV_STYLE(.fg = t->text, .font_size = 22));

    line(c, "Latin",   "caf\xc3\xa9  na\xc3\xafve  Z\xc3\xbcrich  \xc3\x86sop");
    line(c, "Cyrillic","\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82, \xd0\xbc\xd0\xb8\xd1\x80");
    line(c, "Greek",   "\xce\x93\xce\xb5\xce\xb9\xce\xac \xcf\x83\xce\xbf\xcf\x85, \xce\xba\xcf\x8c\xcf\x83\xce\xbc\xce\xb5");
    line(c, "CJK",     "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e  \xe4\xb8\xad\xe6\x96\x87  \xed\x95\x9c\xea\xb5\xad\xec\x96\xb4");
    line(c, "Symbols", "\xe2\x86\x92 \xe2\x96\xb8 \xe2\x80\xa6 \xe2\x98\x85 \xe2\x89\x88 \xe2\x88\x9e");

    vv_text(c, "Editable (type any language, Ctrl-C/V/X):",
            VV_STYLE(.fg = t->text_muted, .font_size = 13));
    vv_text_area(c, "ed", a->text, (int)sizeof a->text, 120, NULL, MSG_EDIT);
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 i18n", 640, 520);
  if (!app) return 1;
  // Fallback order: base script font first, then CJK for the ideographs.
  vv_app_load_font(app, "/usr/share/fonts/noto/NotoSans-Regular.ttf");
  vv_app_load_font(app, "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc");

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_app_bind_clipboard(app, &ctx);
  vv_set_idle_mode(&ctx, true);

  static App state;
  snprintf(state.text, sizeof state.text,
           "Hello \xe4\xb8\x96\xe7\x95\x8c! \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xf0\x9f\x91\x8b\n");

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
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
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
