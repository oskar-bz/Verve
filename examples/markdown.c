// markdown.c — a real split-pane Markdown editor: edit on the left, live
// rendered preview on the right. Shows several framework pieces working
// together in an actual program (not a widget tour):
//
//   • vv_text_area   — the multi-line editor, edits the source buffer in place.
//   • vv_rich_text   — the preview: headings, bold/italic/`code` inline runs,
//                      bullet lists, and fenced code blocks, all styled.
//   • vv_history     — Ctrl-Z / Ctrl-Y (and Edit menu) undo-redo of the source.
//   • native dialogs — File ▸ Open / Save round-trip a .md file.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const vv_Theme *TH;
static vv_FontID BOLD; // a real bold face for **bold** and headings

enum { MSG_EDIT = 1, MSG_OPEN, MSG_SAVE, MSG_UNDO, MSG_REDO };

#define TEXTCAP 16384

typedef struct {
  char       text[TEXTCAP];
  char       path[256];
  char       status[160];
  vv_History hist;
  vv_Ctx    *ctx;
  vv_App    *app;
} App;

// ---- markdown rendering ----------------------------------------------------
// Inline parse: split a line into styled runs at **bold**, *italic*, `code`.
static void inline_spans(vv_Ctx *c, const char *s, int len, vv_Span *sp, int *ns, int cap) {
  int i = 0;
  while (i < len && *ns < cap) {
    if (i + 1 < len && s[i] == '*' && s[i + 1] == '*') {          // **bold**
      int j = i + 2;
      while (j + 1 < len && !(s[j] == '*' && s[j + 1] == '*')) j++;
      sp[(*ns)++] = (vv_Span){vv_fmt(c, "%.*s", j - (i + 2), s + i + 2), TH->text, 0, BOLD};
      i = j + 2;
    } else if (s[i] == '*') {                                      // *italic*
      int j = i + 1;
      while (j < len && s[j] != '*') j++;
      sp[(*ns)++] = (vv_Span){vv_fmt(c, "%.*s", j - (i + 1), s + i + 1), TH->text_muted, 0};
      i = j + 1;
    } else if (s[i] == '`') {                                      // `code`
      int j = i + 1;
      while (j < len && s[j] != '`') j++;
      sp[(*ns)++] = (vv_Span){vv_fmt(c, "%.*s", j - (i + 1), s + i + 1), TH->accent, 0};
      i = j + 1;
    } else {                                                       // plain run
      int j = i;
      while (j < len && s[j] != '*' && s[j] != '`') j++;
      if (j == i) j++;
      sp[(*ns)++] = (vv_Span){vv_fmt(c, "%.*s", j - i, s + i), TH->text, 0};
      i = j;
    }
  }
}

static void render_md(vv_Ctx *c, App *a) {
  const char *p = a->text;
  int line = 0;
  bool incode = false;
  while (*p) {
    const char *nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int)strlen(p);
    const char *key = vv_fmt(c, "l%d", line++);

    if (len >= 3 && strncmp(p, "```", 3) == 0) {                   // fenced toggle
      incode = !incode;
    } else if (incode) {                                          // code line
      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .padding = vv_hv(10, 2)),
             VV_STYLE(.bg = TH->surface_hi)) {
        vv_text(c, len ? vv_fmt(c, "%.*s", len, p) : " ",
                VV_STYLE(.fg = TH->accent, .font_size = TH->font_size - 1));
      }
    } else if (len == 0) {                                        // blank -> gap
      VV_BOX(c, VV_LAYOUT(.h = vv_fixed(8)), VV_STYLE(.bg = {0})) {}
    } else if (p[0] == '#') {                                     // heading
      int h = 0; while (h < len && p[h] == '#') h++;
      int off = h; while (off < len && p[off] == ' ') off++;
      float bump = h == 1 ? 12 : h == 2 ? 7 : 3;
      vv_Span sp[1] = {{vv_fmt(c, "%.*s", len - off, p + off), TH->text, TH->font_size + bump, BOLD}};
      vv_rich_text(c, key, sp, 1);
    } else if ((p[0] == '-' || p[0] == '*') && len > 1 && p[1] == ' ') { // bullet
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 8), VV_STYLE(.bg = {0})) {
        vv_text(c, "\xe2\x80\xa2", VV_STYLE(.fg = TH->accent, .font_size = TH->font_size));
        vv_Span sp[16]; int ns = 0;
        inline_spans(c, p + 2, len - 2, sp, &ns, 16);
        vv_rich_text(c, vv_fmt(c, "%sb", key), sp, ns);
      }
    } else {                                                      // paragraph
      vv_Span sp[24]; int ns = 0;
      inline_spans(c, p, len, sp, &ns, 24);
      vv_rich_text(c, key, sp, ns);
    }
    p = nl ? nl + 1 : p + len;
  }
}

// ---- model -----------------------------------------------------------------
static void snapshot(App *a) { vv_history_push(&a->hist, a->text); }

static void open_cb(void *ud, const char *path) {
  App *a = ud;
  if (!path) { snprintf(a->status, sizeof a->status, "Open cancelled"); goto done; }
  FILE *f = fopen(path, "rb");
  if (!f) { snprintf(a->status, sizeof a->status, "Cannot open %s", path); goto done; }
  size_t n = fread(a->text, 1, TEXTCAP - 1, f);
  a->text[n] = '\0';
  fclose(f);
  snprintf(a->path, sizeof a->path, "%s", path);
  snprintf(a->status, sizeof a->status, "Opened %s", path);
  vv_history_push(&a->hist, a->text);
done:
  if (a->ctx) vv_invalidate(a->ctx);
}
static void save_cb(void *ud, const char *path) {
  App *a = ud;
  if (!path) { snprintf(a->status, sizeof a->status, "Save cancelled"); goto done; }
  FILE *f = fopen(path, "wb");
  if (!f) { snprintf(a->status, sizeof a->status, "Cannot write %s", path); goto done; }
  fwrite(a->text, 1, strlen(a->text), f);
  fclose(f);
  snprintf(a->path, sizeof a->path, "%s", path);
  snprintf(a->status, sizeof a->status, "Saved %s", path);
done:
  if (a->ctx) vv_invalidate(a->ctx);
}

static void update(void *st, vv_Event ev) {
  App *a = st;
  switch (ev.msg) {
  case MSG_EDIT: snapshot(a); break; // text_area already edited a->text in place
  case MSG_OPEN: vv_app_open_file(a->app, "Markdown", "md;markdown;txt", open_cb, a); break;
  case MSG_SAVE: vv_app_save_file(a->app, "Markdown", "md;markdown;txt", "notes.md", save_cb, a); break;
  case MSG_UNDO: if (vv_history_undo(&a->hist, a->text)) snprintf(a->status, sizeof a->status, "Undo"); break;
  case MSG_REDO: if (vv_history_redo(&a->hist, a->text)) snprintf(a->status, sizeof a->status, "Redo"); break;
  }
}

// ---- view ------------------------------------------------------------------
static void toolbtn(vv_Ctx *c, const char *key, const char *label, vv_Msg msg, bool on) {
  if (on) vv_button(c, key, label, msg, VV_NO_PAYLOAD);
  else { // disabled look
    VV_BOX(c, VV_LAYOUT(.padding = vv_hv(14, 8)), VV_STYLE(.bg = TH->surface_hi, .radius = vv_r(TH->radius)))
      vv_text(c, label, VV_STYLE(.fg = TH->text_muted, .font_size = TH->font_size));
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  uint32_t m_file = 0, m_edit = 0;

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
    vv_menubar_begin(c);
    m_file = vv_menu_title(c, "file", "File");
    m_edit = vv_menu_title(c, "edit", "Edit");
    vv_menubar_end(c);

    // Toolbar.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                        .gap = 8, .padding = vv_all(10)),
           VV_STYLE(.bg = t->surface)) {
      vv_button(c, "open", "Open", MSG_OPEN, VV_NO_PAYLOAD);
      vv_button(c, "save", "Save", MSG_SAVE, VV_NO_PAYLOAD);
      toolbtn(c, "undo", "Undo", MSG_UNDO, vv_history_can_undo(&a->hist));
      toolbtn(c, "redo", "Redo", MSG_REDO, vv_history_can_redo(&a->hist));
      VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      vv_text(c, a->status, VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 2));
    }

    // Split: editor | preview.
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)), VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.w = vv_percent(0.5f), .h = vv_grow(1), .padding = vv_all(10)),
             VV_STYLE(.bg = vv_rgb(0.07f, 0.08f, 0.10f))) {
        vv_text_area(c, "src", a->text, TEXTCAP, 0, "Write Markdown...", MSG_EDIT);
      }
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 4,
                          .padding = vv_all(16), .scroll_y = true, .clip = true),
             VV_STYLE(.bg = t->surface,
                      .border_width = (vv_Edges){1, 0, 0, 0}, .border_color = t->border)) {
        render_md(c, a);
      }
    }
  }

  // Menus (overlay).
  if (vv_menu_is_open(c, m_file)) {
    vv_Rect r = vv_node(c, m_file)->actual_rect;
    vv_menu_begin(c, "fm", vv_v2(r.x, r.y + r.h));
    if (vv_menu_item(c, "o", "Open...", "Ctrl+O")) vv_emit(c, MSG_OPEN, VV_NO_PAYLOAD);
    if (vv_menu_item(c, "s", "Save...", "Ctrl+S")) vv_emit(c, MSG_SAVE, VV_NO_PAYLOAD);
    vv_menu_end(c);
  }
  if (vv_menu_is_open(c, m_edit)) {
    vv_Rect r = vv_node(c, m_edit)->actual_rect;
    vv_menu_begin(c, "em", vv_v2(r.x, r.y + r.h));
    if (vv_menu_item(c, "u", "Undo", "Ctrl+Z")) vv_emit(c, MSG_UNDO, VV_NO_PAYLOAD);
    if (vv_menu_item(c, "r", "Redo", "Ctrl+Y")) vv_emit(c, MSG_REDO, VV_NO_PAYLOAD);
    vv_menu_end(c);
  }
}

static const char *SAMPLE =
    "# Verve Markdown\n\n"
    "A **live** editor: type on the left, see it *rendered* on the right.\n\n"
    "## Features\n\n"
    "- **bold**, *italic*, and `inline code`\n"
    "- headings, bullet lists, code blocks\n"
    "- undo/redo with the `vv_history` primitive\n\n"
    "```\n"
    "vv_rich_text(ctx, key, spans, n);\n"
    "```\n\n"
    "Edit this text to see the preview update.\n";

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Markdown", 1040, 700);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;
  const char *bolds[] = {"/usr/share/fonts/noto/NotoSans-Bold.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Bold.ttf", NULL};
  for (int i = 0; bolds[i]; i++) { vv_FontID b = vv_app_load_font(app, bolds[i]);
                                   if (b) { BOLD = b; break; } }

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);
  vv_app_bind_clipboard(app, &ctx);

  static App state;
  state.app = app; state.ctx = &ctx;
  snprintf(state.text, sizeof state.text, "%s", SAMPLE);
  snprintf(state.status, sizeof state.status, "ready");
  vv_history_init(&state.hist, TEXTCAP, 128);
  vv_history_push(&state.hist, state.text);
  TH = vv_theme();

  // Ctrl+O/S/Z/Y global shortcuts, read from the raw input each frame.
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

  vv_history_free(&state.hist);
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
