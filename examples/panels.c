// panels.c — a resizable multi-panel (IDE-shell) layout built from vv_splitter.
//
// The layout is ordinary flexbox: a row of [sidebar | editor-column | inspector],
// where the editor-column is itself [editor / bottom-panel]. What makes it
// *resizable* is that each pane's size lives in app state, and a vv_splitter
// between panes drags that number. Because view() is pure, the splitter never
// mutates state directly — it emits a resize message, update() clamps and stores
// it, and the panes (FLIP-sprung) glide to the new size.
//
// Drag the three dividers; double-clicking a divider resets that pane (the reset
// animates because the pane springs back).
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

enum { MSG_LEFT = 1, MSG_RIGHT, MSG_BOTTOM, MSG_RESET_L, MSG_RESET_R, MSG_RESET_B, MSG_EDIT };

typedef struct {
  float left_w, right_w, bottom_h; // pane sizes, the resizable state
  char  code[4096];
} App;

static const float LEFT0 = 220, RIGHT0 = 260, BOTTOM0 = 160;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void update(void *st, vv_Event ev) {
  App *a = st;
  switch (ev.msg) {
  case MSG_LEFT:   a->left_w   = clampf((float)ev.data.as_float, 140, 460); break;
  case MSG_RIGHT:  a->right_w  = clampf((float)ev.data.as_float, 160, 520); break;
  case MSG_BOTTOM: a->bottom_h = clampf((float)ev.data.as_float, 80, 420);  break;
  case MSG_RESET_L: a->left_w = LEFT0;   break;
  case MSG_RESET_R: a->right_w = RIGHT0; break;
  case MSG_RESET_B: a->bottom_h = BOTTOM0; break;
  case MSG_EDIT: break;
  }
}

static void panel_header(vv_Ctx *c, const char *title) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(30),
                      .cross = VV_ALIGN_CENTER, .padding = vv_hv(12, 0)),
         VV_STYLE(.bg = vv_rgb(0.11f, 0.12f, 0.15f),
                  .border_width = (vv_Edges){0, 0, 0, 1}, .border_color = t->border)) {
    vv_text(c, title, VV_STYLE(.fg = t->text_muted, .font_size = 12));
  }
}

// Trailing panes (the right inspector, the bottom terminal) pass trailing=true
// so a drag toward them shrinks them — no sign juggling in the caller.
static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.08f, 0.09f, 0.11f))) {

    // -- left sidebar -------------------------------------------------------
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(a->left_w), .h = vv_grow(1)),
           VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.13f))) {
      panel_header(c, "EXPLORER");
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                          .padding = vv_all(8), .gap = 2, .scroll_y = true, .clip = true),
             VV_STYLE(.bg = {0})) {
        const char *files[] = {"main.c", "vv_widgets.c", "vv_present.c", "README.md", "Makefile"};
        for (int i = 0; i < 5; i++)
          vv_list_item(c, files[i], files[i], i == 0, MSG_EDIT, VV_NO_PAYLOAD);
      }
    }
    if (vv_double_clicked(c, vv_splitter(c, "sl", VV_ROW, false, a->left_w, 140, 460, MSG_LEFT)))
      vv_emit(c, MSG_RESET_L, VV_NO_PAYLOAD);

    // -- center column: editor over a bottom panel --------------------------
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
           VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
             VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
        panel_header(c, "EDITOR");
        VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1), .padding = vv_all(10)),
               VV_STYLE(.bg = {0})) {
          vv_text_area(c, "code", a->code, (int)sizeof a->code, 0, NULL, MSG_EDIT);
        }
      }
      if (vv_double_clicked(c, vv_splitter(c, "sb", VV_COLUMN, true, a->bottom_h, 80, 420, MSG_BOTTOM)))
        vv_emit(c, MSG_RESET_B, VV_NO_PAYLOAD);
      // bottom panel: its height is the state, so it sits *below* the splitter.
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_fixed(a->bottom_h)),
             VV_STYLE(.bg = vv_rgb(0.07f, 0.08f, 0.10f))) {
        panel_header(c, "TERMINAL");
        VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1), .padding = vv_all(10)),
               VV_STYLE(.bg = {0})) {
          vv_text(c, "$ make gui\nGUI OK\n$ ./build/panels\n",
                  VV_STYLE(.fg = vv_rgb(0.5f, 0.85f, 0.55f), .font_size = 13));
        }
      }
    }

    // The inspector is a trailing pane: the `trailing` flag makes a rightward
    // drag shrink it, so we pass a plain positive width.
    if (vv_double_clicked(c, vv_splitter(c, "sr", VV_ROW, true, a->right_w, 160, 520, MSG_RIGHT)))
      vv_emit(c, MSG_RESET_R, VV_NO_PAYLOAD);

    // -- right inspector ----------------------------------------------------
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(a->right_w), .h = vv_grow(1)),
           VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.13f))) {
      panel_header(c, "INSPECTOR");
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                          .padding = vv_all(14), .gap = 10),
             VV_STYLE(.bg = {0})) {
        vv_text(c, "Panel sizes", VV_STYLE(.fg = t->text, .font_size = 15));
        vv_text(c, vv_fmt(c, "left    %.0f px", (double)a->left_w),
                VV_STYLE(.fg = t->text_muted, .font_size = 13));
        vv_text(c, vv_fmt(c, "right   %.0f px", (double)a->right_w),
                VV_STYLE(.fg = t->text_muted, .font_size = 13));
        vv_text(c, vv_fmt(c, "bottom  %.0f px", (double)a->bottom_h),
                VV_STYLE(.fg = t->text_muted, .font_size = 13));
        vv_text(c, "Drag a divider to resize.\nDouble-click one to reset.",
                VV_STYLE(.fg = t->text_muted, .font_size = 13));
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Resizable Panels", 1040, 660);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  static App state;
  state.left_w = LEFT0; state.right_w = RIGHT0; state.bottom_h = BOTTOM0;
  snprintf(state.code, sizeof state.code,
           "// Drag the dividers between panels.\n"
           "// Each pane's size is app state; the splitter emits a resize\n"
           "// message and the panes spring to the new size.\n\n"
           "int main(void) {\n    return 0;\n}\n");

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.08f, 0.09f, 0.11f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
