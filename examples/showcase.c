// showcase.c — the four "desktop app" features on Verve, in one small note editor:
//
//   • Multi-window     — "Window > New preview" opens a real second OS window
//                        (shared GL context, its own vv_Ctx) mirroring the note.
//   • Native dialogs   — "File > Open.../Save..." call the OS file picker (SDL3).
//   • Multiline editing — the big pane is a full vv_text_area (Enter, Up/Down,
//                        selection, scrolling).
//   • Popovers/tooltips — the toolbar buttons have hover tooltips; "Aa" opens a
//                        popover that dismisses on outside-click or Escape.
//
// Overlays (menus, popovers, tooltips) are built LAST in the view so they paint
// on top — the painter is strict tree order. State stays in AppState; view() is
// pure; update() mutates. The dialog callbacks are the one async seam: the OS
// calls them during the pump, so they write state and vv_invalidate.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

enum {
  MSG_EDIT = 1, MSG_NEW, MSG_OPEN, MSG_SAVE, MSG_QUIT, MSG_NEWWIN,
  MSG_WRAP, MSG_SNIPPET,
};

#define MAX_CHILDREN 4

typedef struct {
  char    text[8192];
  char    status[256];
  vv_Ctx *ctx;            // main window ctx, for dialog callbacks to invalidate
  vv_App *app;            // main window, to anchor native dialogs

  // toolbar button handles (captured in view, used to anchor tooltips/popover)
  uint32_t tb_new, tb_open, tb_save, tb_aa;
  bool     wrap;
  // note: the Aa popover's open state lives in vv_ui_state, not here — see view()

  int  want_windows;      // requested new preview windows (drained by the loop)
  bool quit;
} App;

// ---- native dialog callbacks (fire during the pump, on the event thread) ---
static void on_open(void *ud, const char *path) {
  App *a = ud;
  if (!path) { snprintf(a->status, sizeof a->status, "Open cancelled"); }
  else {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(a->status, sizeof a->status, "Could not open %s", path); }
    else {
      size_t n = fread(a->text, 1, sizeof a->text - 1, f);
      a->text[n] = 0; fclose(f);
      snprintf(a->status, sizeof a->status, "Opened %s (%zu bytes)", path, n);
    }
  }
  if (a->ctx) vv_invalidate(a->ctx);
}
static void on_save(void *ud, const char *path) {
  App *a = ud;
  if (!path) { snprintf(a->status, sizeof a->status, "Save cancelled"); }
  else {
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(a->status, sizeof a->status, "Could not write %s", path); }
    else {
      size_t n = strlen(a->text);
      fwrite(a->text, 1, n, f); fclose(f);
      snprintf(a->status, sizeof a->status, "Saved %s (%zu bytes)", path, n);
    }
  }
  if (a->ctx) vv_invalidate(a->ctx);
}

static void update(void *st, vv_Event ev) {
  App *a = st;
  switch (ev.msg) {
  case MSG_EDIT: snprintf(a->status, sizeof a->status, "%zu chars", strlen(a->text)); break;
  case MSG_NEW:  a->text[0] = 0; snprintf(a->status, sizeof a->status, "New note"); break;
  case MSG_OPEN: vv_app_open_file(a->app, "Text", "txt;md", on_open, a); break;
  case MSG_SAVE: vv_app_save_file(a->app, "Text", "txt;md", "note.txt", on_save, a); break;
  case MSG_QUIT: a->quit = true; break;
  case MSG_NEWWIN: a->want_windows++; break;
  case MSG_WRAP: a->wrap = !a->wrap; break;
  case MSG_SNIPPET: {
    const char *s = ev.data.as_str;
    size_t cur = strlen(a->text), add = strlen(s);
    if (cur + add < sizeof a->text) { memcpy(a->text + cur, s, add + 1); }
    break;
  }
  }
}

static uint32_t toolbar_button(vv_Ctx *c, const char *key, const char *glyph,
                               vv_Msg msg) {
  const vv_Theme *t = vv_theme();
  vv_Style hover = {.bg = t->surface_hi};
  uint32_t id = vv_box_keyed(c, key, strlen(key),
                             (vv_LayoutDecl){.h = vv_fixed(30), .padding = vv_hv(12, 0),
                                             .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER,
                                             .focusable = true},
                             (vv_Style){.bg = t->surface, .radius = vv_r(6),
                                        .border_width = vv_all(1), .border_color = t->border,
                                        .hover = &hover});
  vv_text(c, glyph, VV_STYLE(.fg = t->text, .font_size = 15));
  vv_end_box(c);
  if (msg && vv_clicked(c, id)) vv_emit(c, msg, VV_NO_PAYLOAD); // msg 0 => caller handles the click
  return id;
}

// A menu title + its dropdown, declared together. Overlays set .z, so the
// dropdown lifts above the tree even though it's built here, inline — no
// "build overlays last" bookkeeping.
static void file_menu(vv_Ctx *c, App *a) {
  uint32_t m = vv_menu_title(c, "file", "File");
  if (!vv_menu_is_open(c, m)) return;
  vv_Rect r = vv_node(c, m)->actual_rect;
  vv_menu_begin(c, "filemenu", vv_v2(r.x, r.y + r.h));
  if (vv_menu_item(c, "mnew",  "New",    "Ctrl+N")) vv_emit(c, MSG_NEW, VV_NO_PAYLOAD);
  if (vv_menu_item(c, "mopen", "Open...", "Ctrl+O")) vv_emit(c, MSG_OPEN, VV_NO_PAYLOAD);
  if (vv_menu_item(c, "msave", "Save...", "Ctrl+S")) vv_emit(c, MSG_SAVE, VV_NO_PAYLOAD);
  vv_menu_separator(c);
  if (vv_menu_item(c, "mquit", "Quit", "Ctrl+Q")) vv_emit(c, MSG_QUIT, VV_NO_PAYLOAD);
  vv_menu_end(c);
  (void)a;
}
static void view_menu(vv_Ctx *c, App *a) {
  uint32_t m = vv_menu_title(c, "view", "View");
  if (!vv_menu_is_open(c, m)) return;
  vv_Rect r = vv_node(c, m)->actual_rect;
  vv_menu_begin(c, "viewmenu", vv_v2(r.x, r.y + r.h));
  if (vv_menu_item(c, "vwrap", a->wrap ? "[x] Word wrap" : "[ ] Word wrap", NULL))
    vv_emit(c, MSG_WRAP, VV_NO_PAYLOAD);
  vv_menu_separator(c);
  if (vv_menu_item(c, "vsig", "Insert signature", NULL))
    vv_emit(c, MSG_SNIPPET, vv_ps("\n\n-- \nSent from Verve\n"));
  vv_menu_end(c);
}
static void window_menu(vv_Ctx *c, App *a) {
  uint32_t m = vv_menu_title(c, "window", "Window");
  if (!vv_menu_is_open(c, m)) return;
  vv_Rect r = vv_node(c, m)->actual_rect;
  vv_menu_begin(c, "winmenu", vv_v2(r.x, r.y + r.h));
  if (vv_menu_item(c, "wnew", "New preview window", NULL))
    vv_emit(c, MSG_NEWWIN, VV_NO_PAYLOAD);
  vv_menu_end(c);
  (void)a;
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.13f))) {

    // --- menu bar: title + dropdown declared together, inline ---------------
    vv_menubar_begin(c);
    file_menu(c, a);
    view_menu(c, a);
    window_menu(c, a);
    vv_menubar_end(c);

    // --- toolbar ------------------------------------------------------------
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                        .padding = vv_all(10), .gap = 8),
           VV_STYLE(.bg = {0})) {
      a->tb_new  = toolbar_button(c, "tnew", "New", MSG_NEW);
      a->tb_open = toolbar_button(c, "topn", "Open", MSG_OPEN);
      a->tb_save = toolbar_button(c, "tsav", "Save", MSG_SAVE);
      a->tb_aa   = toolbar_button(c, "taa", "Aa", 0);  // 0 => handled locally below

      // The popover's open flag is view-local: no App field, no message, no
      // update() case — just vv_ui_state keyed by a string.
      bool *aa_open = vv_ui_state(c, "aa-popover", bool);
      if (vv_clicked(c, a->tb_aa)) *aa_open = !*aa_open;
      if (*aa_open) {
        vv_Rect r = vv_node(c, a->tb_aa)->actual_rect;
        vv_popover_open(c, "aapop", vv_v2(r.x, r.y + r.h + 4), 240, aa_open);
        vv_text(c, "Insert snippet", VV_STYLE(.fg = t->text, .font_size = 15));
        vv_button(c, "sdate", "Date stamp", MSG_SNIPPET, vv_ps("[2026-07-19] "));
        vv_button(c, "stodo", "TODO item", MSG_SNIPPET, vv_ps("\n- [ ] "));
        vv_popover_end(c);
      }

      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      vv_text(c, a->status, VV_STYLE(.fg = t->text_muted, .font_size = 13));

      // Tooltips: also z-lifted, so they can live right next to their targets.
      vv_tooltip(c, a->tb_new,  "New note");
      vv_tooltip(c, a->tb_open, "Open... (native dialog)");
      vv_tooltip(c, a->tb_save, "Save... (native dialog)");
      vv_tooltip(c, a->tb_aa,   "Insert snippet");
    }

    // --- editor -------------------------------------------------------------
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                        .padding = (vv_Edges){10, 0, 10, 10}, .gap = 8),
           VV_STYLE(.bg = {0})) {
      vv_text_area(c, "editor", a->text, (int)sizeof a->text, 0, NULL,
                   MSG_EDIT); // height 0 => grow to fill the pane
    }
  }
}

// The preview windows: a second real OS window with its own context, rendering
// a read-only mirror of the note.
static void view_child(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(16), .gap = 10),
         VV_STYLE(.bg = vv_rgb(0.08f, 0.09f, 0.11f))) {
    vv_text(c, "Preview - detached window", VV_STYLE(.fg = t->text_muted, .font_size = 13));
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                        .padding = vv_all(12), .scroll_y = true, .clip = true),
           VV_STYLE(.bg = t->surface, .radius = vv_r(8),
                    .border_width = vv_all(1), .border_color = t->border)) {
      vv_text(c, a->text[0] ? a->text : "(empty note)",
              VV_STYLE(.fg = t->text, .font_size = 15));
    }
  }
}

typedef struct { vv_App *app; vv_Ctx ctx; bool used; } Child;

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Showcase", 900, 640);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);

  static App state;
  state.app = app; state.ctx = &ctx;
  snprintf(state.text, sizeof state.text,
           "# Welcome to the Verve showcase\n\nThis is a real multi-line editor.\n"
           "Try: File > Open..., the toolbar tooltips, the Aa popover,\n"
           "and Window > New preview window.\n");
  snprintf(state.status, sizeof state.status, "%zu chars", strlen(state.text));

  Child children[MAX_CHILDREN] = {0};

  while (vv_app_pump_all() > 0 && !state.quit) {
    static uint64_t prev; if (!prev) prev = SDL_GetPerformanceCounter();
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    if (vv_app_should_close(app)) break; // closing the main window quits

    // Main window.
    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, vv_app_input(app), update, view, &state);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.10f, 0.11f, 0.13f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }

    // Spawn any requested preview windows.
    while (state.want_windows > 0) {
      state.want_windows--;
      for (int i = 0; i < MAX_CHILDREN; i++) {
        if (children[i].used) continue;
        children[i].app = vv_app_open_child(app, "Verve \xc2\xb7 Preview", 420, 480);
        if (!children[i].app) break;
        vv_init(&children[i].ctx);
        vv_set_measure_fn(&children[i].ctx, vv_app_measure, children[i].app);
        children[i].used = true;
        break;
      }
    }

    // Preview windows (each its own context; closing one destroys just it).
    for (int i = 0; i < MAX_CHILDREN; i++) {
      if (!children[i].used) continue;
      if (vv_app_should_close(children[i].app)) {
        vv_shutdown(&children[i].ctx);
        vv_app_destroy(children[i].app);
        children[i].used = false;
        continue;
      }
      int cw, ch; float cdpi; vv_app_size(children[i].app, &cw, &ch, &cdpi);
      vv_set_window(&children[i].ctx, (float)cw, (float)ch, cdpi);
      vv_CommandBuffer *ccmds = vv_run_frame(&children[i].ctx, dt,
                                             vv_app_input(children[i].app),
                                             update, view_child, &state);
      if (ccmds) {
        vv_app_frame_begin(children[i].app, vv_rgb(0.08f, 0.09f, 0.11f));
        vv_render(vv_app_backend(children[i].app), ccmds, cw, ch, cdpi);
        vv_app_frame_end(children[i].app);
      }
    }
  }

  for (int i = 0; i < MAX_CHILDREN; i++)
    if (children[i].used) { vv_shutdown(&children[i].ctx); vv_app_destroy(children[i].app); }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
