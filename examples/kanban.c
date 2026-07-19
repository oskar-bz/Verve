// kanban.c — a drag-and-drop board, the showcase for Verve's retained-tree
// animation. The point of the demo: when you drag a card, it's removed from its
// column's flow and a placeholder gap opens at the drop target. Every other card
// then slides into its new slot on its own — you never animate anything by hand.
// That motion is FLIP springs closing the gap between each card's old and new
// layout_rect, driven entirely by declaring a different tree each frame.
//
//   - press a card        -> it lifts and follows the cursor (floating clone)
//   - move over a column  -> a gap opens where it would land; cards spring aside
//   - release             -> the card drops into the gap
//   - double-click a card -> it fades out (built-in exit animation)
//   - "+ Add"             -> a new card springs in
//
// The whole thing is message/update/view: the view never mutates the board, and
// the board never touches the tree. Build with `make gui`, run ./build/kanban.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define NCOLS   3
#define MAXCARD 64
#define CARDH   56.0f     // fixed card height keeps drop math simple

// ---- messages -------------------------------------------------------------
enum {
  MSG_GRAB = 1,   // payload: card id — a card was pressed, begin dragging it
  MSG_SETDROP,    // payload: (col<<32)|order — where the drag would land now
  MSG_DROP,       // release: commit the move to the current drop target
  MSG_ADD,        // payload: column — append a fresh card
  MSG_DELETE,     // payload: card id — double-clicked, remove it
};

// ---- state ----------------------------------------------------------------
typedef struct {
  int  id;          // stable identity: keys the card node, never reused
  bool alive;
  int  hue;         // index into the tag palette
  char text[40];
} Card;

typedef struct {
  Card pool[MAXCARD];             // card storage, addressed by id
  int  cols[NCOLS][MAXCARD];      // ordered lists of card ids per column
  int  ncol[NCOLS];
  int  next_id;

  // active drag session
  int  drag_id;                   // card being dragged, -1 when idle
  int  drop_col, drop_order;      // computed target slot (trails cursor 1 frame)
} Board;

static const char *TITLES[NCOLS] = {"Backlog", "In Progress", "Done"};
static const vv_Color PALETTE[6] = {
    {0.90f, 0.35f, 0.30f, 1}, {0.95f, 0.65f, 0.20f, 1}, {0.40f, 0.75f, 0.35f, 1},
    {0.30f, 0.65f, 0.95f, 1}, {0.60f, 0.45f, 0.90f, 1}, {0.35f, 0.75f, 0.70f, 1}};

static Card *find(Board *b, int id) {
  if (id < 0) return NULL;
  for (int i = 0; i < MAXCARD; i++)
    if (b->pool[i].alive && b->pool[i].id == id) return &b->pool[i];
  return NULL;
}

// Remove an id from a column's ordered list (shifting the rest down).
static void col_remove(Board *b, int col, int id) {
  int *c = b->cols[col];
  for (int i = 0; i < b->ncol[col]; i++)
    if (c[i] == id) {
      memmove(&c[i], &c[i + 1], (size_t)(b->ncol[col] - i - 1) * sizeof(int));
      b->ncol[col]--;
      return;
    }
}

// Insert an id into a column's list at a clamped index.
static void col_insert(Board *b, int col, int id, int at) {
  int *c = b->cols[col];
  if (at < 0) at = 0;
  if (at > b->ncol[col]) at = b->ncol[col];
  memmove(&c[at + 1], &c[at], (size_t)(b->ncol[col] - at) * sizeof(int));
  c[at] = id;
  b->ncol[col]++;
}

static void add_card(Board *b, int col, const char *text) {
  for (int i = 0; i < MAXCARD; i++)
    if (!b->pool[i].alive) {
      Card *c = &b->pool[i];
      c->alive = true;
      c->id = b->next_id++;
      c->hue = c->id % 6;
      snprintf(c->text, sizeof c->text, "%s", text);
      col_insert(b, col, c->id, b->ncol[col]);
      return;
    }
}

// ---- update: the only place the board mutates -----------------------------
static void update(void *state, vv_Event ev) {
  Board *b = state;
  switch (ev.msg) {
  case MSG_GRAB:
    b->drag_id = (int)ev.data.as_int;
    // Seed the drop slot to the card's current spot so the gap opens exactly
    // where it lifted from, not at col 0 for a frame.
    for (int k = 0; k < NCOLS; k++)
      for (int i = 0; i < b->ncol[k]; i++)
        if (b->cols[k][i] == b->drag_id) { b->drop_col = k; b->drop_order = i; }
    break;
  case MSG_SETDROP:
    b->drop_col   = (int)(ev.data.as_int >> 32);
    b->drop_order = (int)(ev.data.as_int & 0xffffffff);
    break;
  case MSG_DROP: {
    Card *c = find(b, b->drag_id);
    if (c) {
      // find & remove from whichever column currently holds it
      for (int k = 0; k < NCOLS; k++) col_remove(b, k, c->id);
      col_insert(b, b->drop_col, c->id, b->drop_order);
    }
    b->drag_id = -1;
    break;
  }
  case MSG_ADD:
    add_card(b, (int)ev.data.as_int, "New task");
    break;
  case MSG_DELETE: {
    Card *c = find(b, (int)ev.data.as_int);
    if (c) {
      for (int k = 0; k < NCOLS; k++) col_remove(b, k, c->id);
      c->alive = false;
    }
    break;
  }
  }
}

// A single card box. Keyed by id so its identity — and thus its FLIP spring and
// exit animation — survives every reorder. Returns its node handle.
static uint32_t card_view(vv_Ctx *c, Board *b, int id, bool floating) {
  const vv_Theme *t = vv_theme();
  Card *cd = find(b, id);
  if (!cd) return 0;

  char key[16];
  snprintf(key, sizeof key, floating ? "float" : "card%d", id);
  vv_Style hover = {.bg = t->surface_hi, .set = VV_STYLE_BG};

  uint32_t node = vv_box_keyed(
      c, key, strlen(key),
      VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(CARDH),
                .cross = VV_ALIGN_CENTER, .padding = vv_hv(12, 8), .gap = 10),
      VV_STYLE(.bg = t->surface, .radius = vv_r(8), .hover = &hover,
               .border_width = vv_all(1), .border_color = t->border,
               .shadow = floating ? (vv_Shadow){.color = {0, 0, 0, 0.45f},
                                                .offset = {0, 8}, .blur = 24}
                                  : (vv_Shadow){0},
               .transform = floating ? vv_scale(1.04f) : vv_mat_identity()));
  {
    // colored tag stripe
    vv_box_keyed(c, "tag", 3,
                 VV_LAYOUT(.w = vv_fixed(5), .h = vv_fixed(CARDH - 20)),
                 VV_STYLE(.bg = PALETTE[cd->hue], .radius = vv_r(3)));
    vv_text(c, cd->text, VV_STYLE(.fg = t->text, .font_size = 15));
  }
  vv_end_box(c);
  return node;
}

// ---- view: pure function of the board -------------------------------------
static void view(vv_Ctx *c, void *state) {
  Board *b = state;
  const vv_Theme *t = vv_theme();
  vv_Vec2 m = vv_mouse(c);
  bool dragging = b->drag_id >= 0;

  // Handles gathered this frame so we can compute the drop target from real
  // geometry after the tree is built (they hold last frame's actual_rect).
  uint32_t col_node[NCOLS] = {0};
  uint32_t card_node[NCOLS][MAXCARD];
  int      card_count[NCOLS] = {0};

  VV_BOX(c,
         VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                   .padding = vv_all(20), .gap = 16),
         VV_STYLE(.bg = vv_rgb(0.08f, 0.09f, 0.11f))) {

    vv_text(c, "Verve Board", VV_STYLE(.fg = t->text, .font_size = 24));
    vv_text(c, "drag cards between columns · double-click to delete",
            VV_STYLE(.fg = t->text_muted, .font_size = 13));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1), .gap = 16),
           VV_STYLE(.bg = {0})) {
      for (int col = 0; col < NCOLS; col++) {
        char ckey[8];
        snprintf(ckey, sizeof ckey, "col%d", col);
        col_node[col] = vv_box_keyed(
            c, ckey, strlen(ckey),
            VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(12), .gap = 10),
            VV_STYLE(.bg = vv_rgb(0.12f, 0.13f, 0.16f), .radius = vv_r(12)));
        {
          // header
          VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                              .main = VV_ALIGN_SPACE_BETWEEN, .cross = VV_ALIGN_CENTER),
                 VV_STYLE(.bg = {0})) {
            vv_text(c, TITLES[col], VV_STYLE(.fg = t->text, .font_size = 16));
            vv_text(c, vv_fmt(c, "%d", b->ncol[col]),
                    VV_STYLE(.fg = t->text_muted, .font_size = 14));
          }

          // cards, inserting a placeholder gap at the live drop target
          for (int i = 0; i < b->ncol[col]; i++) {
            int id = b->cols[col][i];
            if (dragging && col == b->drop_col && card_count[col] == b->drop_order)
              vv_box_keyed(c, "gap", 3,
                           VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(CARDH)),
                           VV_STYLE(.bg = vv_rgba(1, 1, 1, 0.05f), .radius = vv_r(8),
                                    .border_width = vv_all(2),
                                    .border_color = vv_rgba(1, 1, 1, 0.12f)));
            if (dragging && id == b->drag_id) continue; // lifted out of flow
            uint32_t cn = card_view(c, b, id, false);
            card_node[col][card_count[col]++] = cn;
            if (vv_pressed(c, cn)) vv_emit(c, MSG_GRAB, vv_pi(id));
            if (vv_double_clicked(c, cn)) vv_emit(c, MSG_DELETE, vv_pi(id));
          }
          // placeholder past the last card (drop_order beyond the built count)
          if (dragging && col == b->drop_col && b->drop_order >= card_count[col])
            vv_box_keyed(c, "gap", 3,
                         VV_LAYOUT(.w = vv_grow(1), .h = vv_fixed(CARDH)),
                         VV_STYLE(.bg = vv_rgba(1, 1, 1, 0.05f), .radius = vv_r(8),
                                  .border_width = vv_all(2),
                                  .border_color = vv_rgba(1, 1, 1, 0.12f)));

          vv_button(c, ckey /*stable per col*/, "+ Add", MSG_ADD, vv_pi(col));
        }
        vv_end_box(c);
      }
    }
  }

  // Floating card: built last so it paints on top (emission is tree order).
  // INSTANT_RECT makes its absolute rect track the cursor instead of springing.
  if (dragging && find(b, b->drag_id)) {
    float w = 240;
    if (col_node[0]) {
      vv_Rect cr = vv_node(c, col_node[0])->actual_rect;
      if (cr.w > 0) w = cr.w - 24;
    }
    vv_box_keyed(c, "floatwrap", 9,
                 VV_LAYOUT(.w = vv_fixed(w), .h = vv_fixed(CARDH),
                           .has_absolute = true,
                           .absolute = vv_rect(m.x - w / 2, m.y - CARDH / 2, w, CARDH)),
                 VV_STYLE(.transition_mask = VV_INSTANT_RECT));
    card_view(c, b, b->drag_id, true);
    vv_end_box(c);
  }

  // Compute the drop target from real geometry, then feed it back for next
  // frame's placeholder. Column under the cursor by x, slot by comparing the
  // cursor's y against each card's center.
  if (dragging) {
    int col = b->drop_col;
    for (int k = 0; k < NCOLS; k++) {
      vv_Rect r = vv_node(c, col_node[k])->actual_rect;
      if (m.x >= r.x && m.x < r.x + r.w) { col = k; break; }
    }
    int order = 0;
    for (int i = 0; i < card_count[col]; i++) {
      vv_Rect r = vv_node(c, card_node[col][i])->actual_rect;
      if (m.y > r.y + r.h * 0.5f) order++;
    }
    vv_emit(c, MSG_SETDROP, vv_pi(((int64_t)col << 32) | (uint32_t)order));

    // Release anywhere commits the drop (the card isn't in flow to be clicked).
    if (!c->input.mouse_down) vv_emit(c, MSG_DROP, VV_NO_PAYLOAD);
  }
}

// ---- host loop ------------------------------------------------------------
int main(void) {
  vv_App *app = vv_app_create("Verve · Kanban", 1000, 680);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  Board board = {0};
  board.drag_id = -1;
  const char *seed[NCOLS][3] = {
      {"Design spring solver", "Port layout engine", "Write GUIDE.md"},
      {"Hot-reload demo", "Value bindings", NULL},
      {"Idle mode", "Text field caret", "Fat-pointer strings"}};
  for (int col = 0; col < NCOLS; col++)
    for (int i = 0; i < 3; i++)
      if (seed[col][i]) add_card(&board, col, seed[col][i]);

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &board);
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
