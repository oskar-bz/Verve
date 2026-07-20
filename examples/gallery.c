// gallery.c — the new input widgets in one screen: radio, progress, stepper,
// tabs, combobox, tree, plus a right-click context menu, clipboard editing
// (Ctrl-C/V/X in the field), and cursor shapes (hover the field / a control).
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>

enum {
  MSG_TAB = 1, MSG_RADIO, MSG_COMBO, MSG_STEP, MSG_TEXT,
  MSG_TREE, MSG_CTX_OPEN, MSG_CTX_CLOSE, MSG_CTX_ACT,
  MSG_ADV, MSG_MODAL_OPEN, MSG_MODAL_CLOSE,
};

// A tiny file tree: name, depth, is-leaf, parent index.
typedef struct { const char *name; int depth; bool leaf; int parent; } TreeNode;
static const TreeNode TREE[] = {
  {"src", 0, false, -1}, {"vv_widgets.c", 1, true, 0}, {"vv_layout.c", 1, true, 0},
  {"include", 0, false, -1}, {"verve.h", 1, true, 3}, {"vv_style.h", 1, true, 3},
  {"README.md", 0, true, -1},
};
enum { NTREE = (int)(sizeof TREE / sizeof TREE[0]) };

typedef struct {
  int    tab, radio, combo, tree_sel;
  double freq;
  char   text[64];
  bool   expanded[NTREE];
  bool   ctx_open;
  vv_Vec2 ctx_at;
  bool   adv_open, modal_open;
  char   status[96];
} App;

static void update(void *st, vv_Event ev) {
  App *a = st;
  switch (ev.msg) {
  case MSG_TAB:   a->tab = (int)ev.data.as_int; break;
  case MSG_RADIO: a->radio = (int)ev.data.as_int; break;
  case MSG_COMBO: a->combo = (int)ev.data.as_int; break;
  case MSG_STEP:  a->freq = ev.data.as_float; break;
  case MSG_TREE: {
    int i = (int)ev.data.as_int;
    if (TREE[i].leaf) a->tree_sel = i;
    else a->expanded[i] = !a->expanded[i];
    break;
  }
  case MSG_CTX_OPEN:  a->ctx_open = true; a->ctx_at = vv_as_v2(ev.data); break;
  case MSG_CTX_CLOSE: a->ctx_open = false; break;
  case MSG_CTX_ACT:   snprintf(a->status, sizeof a->status, "Context action: %s", ev.data.as_str); a->ctx_open = false; break;
  case MSG_ADV:         a->adv_open = (bool)ev.data.as_int; break;
  case MSG_MODAL_OPEN:  a->modal_open = true; break;
  case MSG_MODAL_CLOSE: a->modal_open = false; break;
  case MSG_TEXT: break;
  }
}

static void inputs_tab(vv_Ctx *c, App *a) {
  const vv_Theme *t = vv_theme();
  static const char *const WAVES[] = {"Sine", "Square", "Saw", "Triangle"};

  vv_text(c, "Radio group", VV_STYLE(.fg = t->text_muted, .font_size = 12));
  for (int i = 0; i < 3; i++)
    vv_radio(c, vv_fmt(c, "r%d", i), (const char *[]){"Low", "Medium", "High"}[i],
             a->radio == i, MSG_RADIO, vv_pi(i));

  vv_text(c, "Select", VV_STYLE(.fg = t->text_muted, .font_size = 12));
  vv_combobox(c, "wave", WAVES, 4, a->combo, MSG_COMBO);

  vv_text(c, "Stepper + progress", VV_STYLE(.fg = t->text_muted, .font_size = 12));
  vv_stepper(c, "freq", a->freq, 10, 0, 100, "Hz", MSG_STEP);
  vv_progress(c, "prog", (float)(a->freq / 100.0));

  vv_text(c, "Text field (Ctrl-C/V/X)", VV_STYLE(.fg = t->text_muted, .font_size = 12));
  vv_text_field(c, "tf", a->text, (int)sizeof a->text, "Type, select, copy...", MSG_TEXT);

  vv_text(c, "Collapsible + modal", VV_STYLE(.fg = t->text_muted, .font_size = 12));
  if (vv_collapsible_begin(c, "adv", "Advanced options", a->adv_open, MSG_ADV)) {
    vv_checkbox(c, "a1", "Dither output", a->radio == 1, MSG_RADIO);
    vv_text(c, "Body FLIP-springs open and closed.",
            VV_STYLE(.fg = t->text_muted, .font_size = 12));
    vv_collapsible_end(c);
  }
  vv_button(c, "openm", "Open dialog...", MSG_MODAL_OPEN, VV_NO_PAYLOAD);
}

static void tree_tab(vv_Ctx *c, App *a) {
  const vv_Theme *t = vv_theme();
  vv_text(c, "File tree (click folders to expand)", VV_STYLE(.fg = t->text_muted, .font_size = 12));
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 1), VV_STYLE(.bg = {0})) {
    for (int i = 0; i < NTREE; i++) {
      // Visible if top-level or its parent is expanded.
      bool visible = TREE[i].parent < 0 || a->expanded[TREE[i].parent];
      if (!visible) continue;
      if (vv_tree_item(c, TREE[i].name, TREE[i].name, TREE[i].depth,
                       TREE[i].leaf, a->expanded[i], a->tree_sel == i))
        vv_emit(c, MSG_TREE, vv_pi(i));
    }
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  static const char *const TABS[] = {"Inputs", "Tree"};

  uint32_t root = vv_box_keyed(c, "root", 4,
      VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .padding = vv_all(20), .gap = 14),
      VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f)));
  vv_text(c, "Widget gallery  (right-click for a context menu)",
          VV_STYLE(.fg = t->text, .font_size = 18));
  vv_tabs(c, "tabs", TABS, 2, a->tab, MSG_TAB);

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 8,
                      .padding = vv_all(16)),
         VV_STYLE(.bg = vv_rgb(0.12f, 0.13f, 0.16f), .radius = vv_r(12),
                  .border_width = vv_all(1), .border_color = t->border)) {
    if (a->tab == 0) inputs_tab(c, a);
    else             tree_tab(c, a);
  }
  vv_text(c, a->status, VV_STYLE(.fg = t->text_muted, .font_size = 12));
  vv_end_box(c);

  // Right-click anywhere in the window opens a context menu at the cursor.
  if (vv_right_clicked(c, root)) vv_emit(c, MSG_CTX_OPEN, vv_pv2(vv_mouse(c)));

  // Overlay: the context menu (z-lifts).
  if (a->ctx_open) {
    vv_context_menu_begin(c, "ctx", a->ctx_at, &a->ctx_open);
    if (vv_menu_item(c, "cut", "Cut", "Ctrl+X"))    vv_emit(c, MSG_CTX_ACT, vv_ps("Cut"));
    if (vv_menu_item(c, "copy", "Copy", "Ctrl+C"))  vv_emit(c, MSG_CTX_ACT, vv_ps("Copy"));
    if (vv_menu_item(c, "paste", "Paste", "Ctrl+V")) vv_emit(c, MSG_CTX_ACT, vv_ps("Paste"));
    vv_menu_separator(c);
    if (vv_menu_item(c, "del", "Delete", NULL))     vv_emit(c, MSG_CTX_ACT, vv_ps("Delete"));
    vv_context_menu_end(c);
  }

  // Overlay: a centered modal dialog (scrim + Escape dismiss).
  if (a->modal_open) {
    vv_modal_begin(c, "dlg", 320, MSG_MODAL_CLOSE);
    vv_text(c, "Delete project?", VV_STYLE(.fg = t->text, .font_size = 18));
    vv_text(c, "This can't be undone. The scrim or Escape dismisses.",
            VV_STYLE(.fg = t->text_muted, .font_size = 13));
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 8,
                        .main = VV_ALIGN_END),
           VV_STYLE(.bg = {0})) {
      vv_button(c, "cancel", "Cancel", MSG_MODAL_CLOSE, VV_NO_PAYLOAD);
      vv_button(c, "ok", "Delete", MSG_MODAL_CLOSE, VV_NO_PAYLOAD);
    }
    vv_modal_end(c);
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Widget Gallery", 560, 620);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_app_bind_clipboard(app, &ctx);   // Ctrl-C/V/X in the text field
  vv_set_idle_mode(&ctx, true);

  App state = {.freq = 40, .combo = 0, .tree_sel = -1};
  snprintf(state.status, sizeof state.status, "ready");
  state.expanded[0] = true; // src open

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
    vv_app_set_cursor(app, vv_cursor(&ctx));  // hovered node's cursor shape
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
