// dragdrop.c — a small board: drag cards between columns. Shows the drag-and-
// drop payload API (vv_drag_source / vv_drop_target): a card carries its index
// as it's dragged; the column it's released over receives it and the model
// moves the card. view() stays pure — the drop only emits a move message.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const vv_Theme *TH;

enum { MSG_MOVE = 1 };
#define NCARD 7
#define NCOL 3

typedef struct { const char *text; int col; } Card;
typedef struct { Card card[NCARD]; vv_Ctx *ctx; } App;

static const char *COLS[NCOL] = {"To do", "Doing", "Done"};

static void update(void *st, vv_Event ev) {
  App *a = st;
  if (ev.msg == MSG_MOVE) {
    vv_Vec2 v = vv_as_v2(ev.data); // x = card index, y = destination column
    int i = (int)v.x, col = (int)v.y;
    if (i >= 0 && i < NCARD && col >= 0 && col < NCOL) a->card[i].col = col;
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  bool dragging = vv_dnd_active(c);

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .gap = 10, .padding = vv_all(18)),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
    vv_text(c, "Board  (drag cards between columns)",
            VV_STYLE(.fg = TH->text, .font_size = TH->font_size + 6));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1), .gap = 12),
           VV_STYLE(.bg = {0})) {
      for (int col = 0; col < NCOL; col++) {
        // A column is a drop target; highlight it while a drag is in progress.
        vv_Style border = {.border_color = dragging ? TH->accent : TH->border};
        uint32_t cid = vv_box_keyed(c, vv_fmt(c, "col%d", col), 0,
            VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 8,
                            .padding = vv_all(12)),
            VV_STYLE(.bg = vv_rgb(0.12f, 0.13f, 0.16f), .radius = vv_r(10),
                       .border_width = vv_all(dragging ? 2.0f : 1.0f),
                       .border_color = border.border_color));
        vv_text(c, COLS[col], VV_STYLE(.fg = TH->text_muted, .font_size = TH->font_size - 2));

        for (int i = 0; i < NCARD; i++) {
          if (a->card[i].col != col) continue;
          uint32_t card = vv_box_keyed(c, vv_fmt(c, "card%d", i), 0,
              VV_LAYOUT(.w = vv_grow(1), .padding = vv_all(12), .focusable = true,
                              .cursor = VV_CURSOR_POINTER),
              VV_STYLE(.bg = TH->surface_hi, .radius = vv_r(TH->radius),
                         .border_width = vv_all(TH->border_width), .border_color = TH->border));
          vv_text(c, a->card[i].text, VV_STYLE(.fg = TH->text, .font_size = TH->font_size));
          vv_end_box(c);
          vv_drag_source(c, card, vv_pi(i)); // carry the card index while dragged
        }
        vv_end_box(c);

        // Drop: if a card is released over this column, move it here.
        vv_Payload pl;
        if (vv_drop_target(c, cid, &pl))
          vv_emit(c, MSG_MOVE, vv_pv2(vv_v2((float)pl.as_int, (float)col)));
      }
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Drag & Drop", 820, 560);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);
  TH = vv_theme();

  static App state;
  const char *names[NCARD] = {"Wireframe", "Ship v1", "Fix crash", "Write docs",
                              "Refactor", "Review PR", "Plan sprint"};
  int cols[NCARD] = {0, 0, 1, 1, 2, 0, 1};
  for (int i = 0; i < NCARD; i++) { state.card[i].text = names[i]; state.card[i].col = cols[i]; }
  state.ctx = &ctx;

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
