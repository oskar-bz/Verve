// sevenguis.c — the 7GUIs benchmark (https://eugenkiss.github.io/7guis/)
// implemented on Verve in the message/update/view style (The Elm Architecture).
// Counter, Temperature, Flight Booker, Timer, CRUD, Circle Drawer, Cells.
//
// All application state lives in one App struct. view() is a pure function of
// that state: it renders and EMITS messages, but never mutates. update() is the
// only place state changes. vv_run_frame drains messages into update() and
// rebuilds view() only when something changed — the Timer keeps itself alive by
// emitting a tick each frame while it runs.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const vv_Theme *TH;

// --------------------------------------------------------------- messages
enum {
  MSG_TICK = 1,     // as_float = dt        (timer heartbeat)
  MSG_SELECT_TASK,  // as_int   = task index

  MSG_COUNT,        // as_int   = delta

  MSG_TEMP_C,       // celsius buffer edited (recompute fahrenheit)
  MSG_TEMP_F,       // fahrenheit buffer edited (recompute celsius)

  MSG_FLIP_TYPE,
  MSG_BOOK,

  MSG_TIMER_DUR,    // as_float = new duration
  MSG_TIMER_RESET,

  MSG_CRUD_SELECT,  // as_int = index
  MSG_CRUD_CREATE,
  MSG_CRUD_UPDATE,
  MSG_CRUD_DELETE,

  MSG_CIRCLE_UNDO,
  MSG_CIRCLE_REDO,
  MSG_CANVAS_CLICK, // as_int = pack2f(local x, y)
  MSG_CIRCLE_DIAM,  // as_float = new diameter

  MSG_CELL_EDIT,    // as_int = r * CELL_COLS + col
};

// ----------------------------------------------------------------- state
#define CELL_ROWS 12
#define CELL_COLS 6

typedef struct { float x, y, r; } Circle;
typedef struct { Circle c[64]; int n; } CircleSet;

typedef struct {
  int task;

  int64_t count;

  char cel[16], fah[16];

  int  flight_type; // 0 one-way, 1 return
  char d1[16], d2[16];
  bool booked;

  float elapsed, duration;

  struct { char name[24], surname[24]; } db[128];
  int  db_n, sel;
  char filter[24], nm[24], sn[24];

  CircleSet hist[128];
  int hist_n, hist_i, csel;

  char cells[CELL_ROWS][CELL_COLS][32];
  int  edit_r, edit_c;
  bool edit_fresh;
} App;

// Pack two float32 into one int64 so a click can carry a 2D point in a payload.
static int64_t pack2f(float a, float b) {
  uint32_t x, y;
  memcpy(&x, &a, sizeof x);
  memcpy(&y, &b, sizeof y);
  return (int64_t)(((uint64_t)x << 32) | y);
}
static void unpack2f(int64_t v, float *a, float *b) {
  uint32_t x = (uint32_t)((uint64_t)v >> 32), y = (uint32_t)v;
  memcpy(a, &x, sizeof *a);
  memcpy(b, &y, sizeof *b);
}

// ------------------------------------------------------- circle history
static CircleSet *cur_set(App *s) { return &s->hist[s->hist_i]; }
static void push_hist(App *s) {
  if (s->hist_i + 1 >= 128) return;
  s->hist[s->hist_i + 1] = s->hist[s->hist_i];
  s->hist_i++;
  s->hist_n = s->hist_i + 1;
}

// ----------------------------------------------------------- formula eval
static double eval_cell(App *s, int r, int c, int depth);
static double eval_expr(App *s, const char *e, int depth) {
  double acc = 0;
  int sign = 1;
  const char *p = e;
  while (*p) {
    while (*p == ' ') p++;
    double term = 0;
    if (*p >= 'A' && *p <= 'Z') {
      int cc = *p - 'A';
      p++;
      int rr = 0;
      while (*p >= '0' && *p <= '9') rr = rr * 10 + (*p++ - '0');
      rr -= 1;
      if (rr >= 0 && rr < CELL_ROWS && cc >= 0 && cc < CELL_COLS)
        term = eval_cell(s, rr, cc, depth + 1);
    } else {
      char *end;
      term = strtod(p, &end);
      p = end;
    }
    acc += sign * term;
    while (*p == ' ') p++;
    if (*p == '+') { sign = 1; p++; }
    else if (*p == '-') { sign = -1; p++; }
    else break;
  }
  return acc;
}
static double eval_cell(App *s, int r, int c, int depth) {
  if (depth > 32) return 0; // cycle guard
  const char *str = s->cells[r][c];
  if (str[0] == '=') return eval_expr(s, str + 1, depth);
  char *end;
  double v = strtod(str, &end);
  return end != str ? v : 0;
}

// ================================================================ UPDATE
static void update(void *state, vv_Event ev) {
  App *s = state;
  switch (ev.msg) {
  case MSG_TICK:
    if (s->elapsed < s->duration)
      s->elapsed = fminf(s->elapsed + (float)ev.data.as_float, s->duration);
    break;
  case MSG_SELECT_TASK: s->task = (int)ev.data.as_int; break;

  case MSG_COUNT: s->count += ev.data.as_int; break;

  case MSG_TEMP_C: {
    char *end;
    double v = strtod(s->cel, &end);
    if (end != s->cel) snprintf(s->fah, sizeof s->fah, "%.1f", v * 9.0 / 5.0 + 32.0);
    break;
  }
  case MSG_TEMP_F: {
    char *end;
    double v = strtod(s->fah, &end);
    if (end != s->fah) snprintf(s->cel, sizeof s->cel, "%.1f", (v - 32.0) * 5.0 / 9.0);
    break;
  }

  case MSG_FLIP_TYPE: s->flight_type ^= 1; s->booked = false; break;
  case MSG_BOOK:      s->booked = true; break;

  case MSG_TIMER_DUR:   s->duration = (float)ev.data.as_float; break;
  case MSG_TIMER_RESET: s->elapsed = 0; break;

  case MSG_CRUD_SELECT:
    s->sel = (int)ev.data.as_int;
    strcpy(s->nm, s->db[s->sel].name);
    strcpy(s->sn, s->db[s->sel].surname);
    break;
  case MSG_CRUD_CREATE:
    if (s->db_n < 128) {
      strcpy(s->db[s->db_n].name, s->nm);
      strcpy(s->db[s->db_n].surname, s->sn);
      s->db_n++;
    }
    break;
  case MSG_CRUD_UPDATE:
    if (s->sel >= 0) {
      strcpy(s->db[s->sel].name, s->nm);
      strcpy(s->db[s->sel].surname, s->sn);
    }
    break;
  case MSG_CRUD_DELETE:
    if (s->sel >= 0) {
      for (int i = s->sel; i < s->db_n - 1; i++) s->db[i] = s->db[i + 1];
      s->db_n--;
      s->sel = -1;
      s->nm[0] = s->sn[0] = 0;
    }
    break;

  case MSG_CIRCLE_UNDO: if (s->hist_i > 0) { s->hist_i--; s->csel = -1; } break;
  case MSG_CIRCLE_REDO: if (s->hist_i < s->hist_n - 1) { s->hist_i++; s->csel = -1; } break;
  case MSG_CANVAS_CLICK: {
    float lx, ly;
    unpack2f(ev.data.as_int, &lx, &ly);
    CircleSet *cs = cur_set(s);
    int hit = -1;
    float best = 1e9f;
    for (int i = 0; i < cs->n; i++) {
      float dx = cs->c[i].x - lx, dy = cs->c[i].y - ly;
      float d = sqrtf(dx * dx + dy * dy);
      if (d < cs->c[i].r && d < best) { best = d; hit = i; }
    }
    if (hit >= 0) {
      s->csel = hit;
    } else if (cs->n < 64) {
      push_hist(s);
      CircleSet *ns = cur_set(s);
      ns->c[ns->n++] = (Circle){lx, ly, 26};
      s->csel = ns->n - 1;
    }
    break;
  }
  case MSG_CIRCLE_DIAM:
    if (s->csel >= 0 && s->csel < cur_set(s)->n)
      cur_set(s)->c[s->csel].r = (float)ev.data.as_float * 0.5f;
    break;

  case MSG_CELL_EDIT:
    s->edit_r = (int)ev.data.as_int / CELL_COLS;
    s->edit_c = (int)ev.data.as_int % CELL_COLS;
    s->edit_fresh = true;
    break;
  }
}

// ================================================================== VIEW
static void title(vv_Ctx *c, const char *str) {
  vv_text(c, str, (vv_Style){.fg = TH->text, .font_size = 22});
}

// A button with an enabled flag that emits `msg` (payload `arg`) on click.
static void button_en(vv_Ctx *c, const char *key, const char *label,
                      bool enabled, vv_Msg msg, vv_Payload arg) {
  vv_Style hover = {.bg = TH->accent_hi};
  vv_Style active = {.bg = TH->accent_lo, .transform = vv_scale(0.97f)};
  uint32_t id = vv_box_keyed(c, key, strlen(key),
                             (vv_LayoutDecl){.w = vv_fit(),
                                             .h = vv_fixed(36),
                                             .padding = vv_hv(16, 8),
                                             .main = VV_ALIGN_CENTER,
                                             .cross = VV_ALIGN_CENTER,
                                             .focusable = enabled,
                                             .disabled = !enabled},
                             (vv_Style){.bg = enabled ? TH->accent : TH->track,
                                        .radius = vv_r(TH->radius),
                                        .hover = enabled ? &hover : NULL,
                                        .active = enabled ? &active : NULL});
  vv_text(c, label,
          (vv_Style){.fg = enabled ? TH->on_accent : TH->text_muted,
                     .font_size = 15});
  vv_end_box(c);
  if (enabled && vv_clicked(c, id)) vv_emit(c, msg, arg);
}

// Fixed-width wrapper around the grow-width text field. `change` is emitted on
// edit; pass VV_MSG_NONE when the buffer itself is the model and no logic runs.
static void field_w(vv_Ctx *c, const char *key, char *buf, int cap,
                    const char *ph, float w, vv_Msg change) {
  vv_box_keyed(c, key, strlen(key),
               (vv_LayoutDecl){.w = vv_fixed(w), .h = vv_fixed(34)},
               (vv_Style){0});
  {
    char sub[40];
    snprintf(sub, sizeof sub, "%s_f", key);
    vv_text_field(c, sub, buf, cap, ph, change);
  }
  vv_end_box(c);
}

// 1 Counter
static void task_counter(vv_Ctx *c, App *s) {
  title(c, "Counter");
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 12, .cross = VV_ALIGN_CENTER}),
         (vv_Style){0}) {
    vv_box_keyed(c, "disp", 4,
                 (vv_LayoutDecl){.w = vv_fixed(80), .h = vv_fixed(36),
                                 .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER},
                 (vv_Style){.bg = TH->surface, .radius = vv_r(6)});
    vv_text(c, vv_fmt(c, "%lld", (long long)s->count),
            (vv_Style){.fg = TH->text, .font_size = 16});
    vv_end_box(c);
    vv_button(c, "inc", "Count", MSG_COUNT, vv_pi(1));
  }
}

// 2 Temperature
static void task_temp(vv_Ctx *c, App *s) {
  title(c, "Temperature Converter");
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER}),
         (vv_Style){0}) {
    field_w(c, "cel", s->cel, sizeof s->cel, "0", 90, MSG_TEMP_C);
    vv_label(c, "Celsius  =");
    field_w(c, "fah", s->fah, sizeof s->fah, "32", 90, MSG_TEMP_F);
    vv_label(c, "Fahrenheit");
  }
}

// 3 Flight Booker
static bool parse_date(const char *str, int *out) {
  int d, m, y;
  if (sscanf(str, "%d.%d.%d", &d, &m, &y) != 3) return false;
  if (d < 1 || d > 31 || m < 1 || m > 12) return false;
  *out = y * 372 + m * 31 + d;
  return true;
}
static void task_flight(vv_Ctx *c, App *s) {
  title(c, "Flight Booker");
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 10, .w = vv_fixed(240)}),
         (vv_Style){0}) {
    const char *types[] = {"one-way flight", "return flight"};
    vv_button(c, "type", types[s->flight_type], MSG_FLIP_TYPE, VV_NO_PAYLOAD);

    int t1, t2;
    bool v1 = parse_date(s->d1, &t1), v2 = parse_date(s->d2, &t2);
    field_w(c, "d1", s->d1, sizeof s->d1, "DD.MM.YYYY", 240, VV_MSG_NONE);
    VV_BOX(c, ((vv_LayoutDecl){.w = vv_grow(1), .disabled = s->flight_type == 0}),
           ((vv_Style){.opacity = s->flight_type == 0 ? 0.4f : 1.0f})) {
      field_w(c, "d2", s->d2, sizeof s->d2, "DD.MM.YYYY", 240, VV_MSG_NONE);
    }

    bool ok = v1 && (s->flight_type == 0 || (v2 && t2 >= t1));
    button_en(c, "book", "Book", ok, MSG_BOOK, VV_NO_PAYLOAD);
    if (s->booked) {
      vv_Str msg = s->flight_type == 0
                       ? vv_fmt(c, "Booked one-way for %s.", s->d1)
                       : vv_fmt(c, "Booked %s → %s.", s->d1, s->d2);
      vv_text(c, msg, (vv_Style){.fg = TH->accent_hi, .font_size = 14});
    }
  }
}

// 4 Timer
static void task_timer(vv_Ctx *c, App *s) {
  title(c, "Timer");
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 14, .w = vv_fixed(320)}),
         (vv_Style){0}) {
    float frac = s->duration > 0 ? s->elapsed / s->duration : 0;
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER}),
           (vv_Style){0}) {
      vv_label(c, "Elapsed:");
      vv_box_keyed(c, "gauge", 5,
                   (vv_LayoutDecl){.w = vv_grow(1), .h = vv_fixed(18)},
                   (vv_Style){.bg = TH->track, .radius = vv_r(9)});
      {
        vv_box_keyed(c, "fill", 1,
                     (vv_LayoutDecl){.has_absolute = true,
                                     .absolute = vv_rect(0, 0, frac * 220.0f, 18)},
                     (vv_Style){.bg = TH->accent, .radius = vv_r(9),
                                .transition_mask = VV_INSTANT_RECT});
        vv_end_box(c);
      }
      vv_end_box(c);
    }
    vv_label(c, vv_fmt(c, "%.1f s", (double)s->elapsed));
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER}),
           (vv_Style){0}) {
      vv_label(c, "Duration:");
      vv_slider(c, "dur", s->duration, 1, 30, MSG_TIMER_DUR);
    }
    vv_button(c, "reset", "Reset", MSG_TIMER_RESET, VV_NO_PAYLOAD);
  }
}

// 5 CRUD
static void task_crud(vv_Ctx *c, App *s) {
  printf("build!\n");
  
  title(c, "CRUD");
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 10, .w = vv_grow(1), .h = vv_grow(1)}),
         (vv_Style){0}) {
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER}),
           (vv_Style){0}) {
      vv_label(c, "Filter:");
      field_w(c, "flt", s->filter, sizeof s->filter, "surname prefix", 180, VV_MSG_NONE);
    }
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 14, .w = vv_grow(1), .h = vv_grow(1)}),
           (vv_Style){0}) {
      VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 4, .w = vv_fixed(240),
                                 .h = vv_grow(1), .padding = vv_all(6),
                                 .scroll_y = true, .clip = true}),
             ((vv_Style){.bg = TH->surface, .radius = vv_r(8)})) {
        for (int i = 0; i < s->db_n; i++) {
          if (s->filter[0] &&
              strncmp(s->db[i].surname, s->filter, strlen(s->filter)) != 0)
            continue;
          vv_list_item(c, vv_fmt(c, "it%d", i),
                       vv_fmt(c, "%s, %s", s->db[i].surname, s->db[i].name),
                       s->sel == i, MSG_CRUD_SELECT, vv_pi(i));
        }
      }
      VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 8, .w = vv_fixed(220)}),
             (vv_Style){0}) {
        VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER}),
               (vv_Style){0}) {
          vv_label(c, "Name:");
          field_w(c, "nm", s->nm, sizeof s->nm, "", 150, VV_MSG_NONE);
        }
        VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 8, .cross = VV_ALIGN_CENTER}),
               (vv_Style){0}) {
          vv_label(c, "Surname:");
          field_w(c, "sn", s->sn, sizeof s->sn, "", 130, VV_MSG_NONE);
        }
      }
    }
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 8}), (vv_Style){0}) {
      button_en(c, "create", "Create", s->nm[0] || s->sn[0], MSG_CRUD_CREATE, VV_NO_PAYLOAD);
      button_en(c, "update", "Update", s->sel >= 0, MSG_CRUD_UPDATE, VV_NO_PAYLOAD);
      button_en(c, "delete", "Delete", s->sel >= 0, MSG_CRUD_DELETE, VV_NO_PAYLOAD);
    }
  }
}

// 6 Circle Drawer
static void task_circle(vv_Ctx *c, App *s) {
  title(c, "Circle Drawer");
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 10, .w = vv_grow(1), .h = vv_grow(1)}),
         (vv_Style){0}) {
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 8, .main = VV_ALIGN_CENTER}),
           (vv_Style){0}) {
      button_en(c, "undo", "Undo", s->hist_i > 0, MSG_CIRCLE_UNDO, VV_NO_PAYLOAD);
      button_en(c, "redo", "Redo", s->hist_i < s->hist_n - 1, MSG_CIRCLE_REDO, VV_NO_PAYLOAD);
    }
    uint32_t canvas = vv_box_keyed(
        c, "canvas", 6,
        (vv_LayoutDecl){.w = vv_grow(1), .h = vv_grow(1), .clip = true},
        (vv_Style){.bg = TH->surface, .radius = vv_r(8)});
    {
      CircleSet *cs = cur_set(s);
      for (int i = 0; i < cs->n; i++) {
        Circle *ci = &cs->c[i];
        bool sel = (i == s->csel);
        vv_box_keyed(c, "c", (uint32_t)(100 + i),
                     (vv_LayoutDecl){.has_absolute = true,
                                     .absolute = vv_rect(ci->x - ci->r, ci->y - ci->r,
                                                         ci->r * 2, ci->r * 2)},
                     (vv_Style){.bg = sel ? TH->accent_lo : vv_rgba(1, 1, 1, 0.06f),
                                .radius = vv_r(ci->r),
                                .border_width = vv_all(2),
                                .border_color = TH->text});
        vv_end_box(c);
      }
      if (vv_clicked(c, canvas)) {
        vv_Rect cr = vv_node(c, canvas)->actual_rect;
        vv_Vec2 m = vv_mouse(c);
        vv_emit(c, MSG_CANVAS_CLICK, vv_pi(pack2f(m.x - cr.x, m.y - cr.y)));
      }
    }
    vv_end_box(c);
    if (s->csel >= 0 && s->csel < cur_set(s)->n) {
      VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 10, .cross = VV_ALIGN_CENTER}),
             (vv_Style){0}) {
        vv_label(c, "Diameter:");
        vv_slider(c, "diam", cur_set(s)->c[s->csel].r * 2, 8, 160, MSG_CIRCLE_DIAM);
      }
    }
  }
}

// 7 Cells
static void task_cells(vv_Ctx *c, App *s) {
  title(c, "Cells");
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 0, .w = vv_grow(1),
                             .h = vv_grow(1), .scroll_y = true, .clip = true}),
         ((vv_Style){.bg = TH->surface, .radius = vv_r(8)})) {
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 1, .padding = vv_hv(1, 1)}),
           (vv_Style){0}) {
      vv_box_keyed(c, "corner", 1,
                   (vv_LayoutDecl){.w = vv_fixed(36), .h = vv_fixed(26)},
                   (vv_Style){0});
      vv_end_box(c);
      for (int col = 0; col < CELL_COLS; col++) {
        char h[4] = {(char)('A' + col), 0};
        vv_box_keyed(c, vv_fmt(c, "h%d", col), 0,
                     (vv_LayoutDecl){.w = vv_fixed(88), .h = vv_fixed(26),
                                     .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER},
                     (vv_Style){.bg = TH->surface_hi});
        vv_text(c, h, (vv_Style){.fg = TH->text_muted, .font_size = 13});
        vv_end_box(c);
      }
    }
    for (int r = 0; r < CELL_ROWS; r++) {
      VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .gap = 1, .padding = vv_hv(1, 0)}),
             (vv_Style){0}) {
        vv_box_keyed(c, vv_fmt(c, "r%d", r), 0,
                     (vv_LayoutDecl){.w = vv_fixed(36), .h = vv_fixed(34),
                                     .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER},
                     (vv_Style){.bg = TH->surface_hi});
        vv_text(c, vv_fmt(c, "%d", r + 1), (vv_Style){.fg = TH->text_muted, .font_size = 13});
        vv_end_box(c);
        for (int col = 0; col < CELL_COLS; col++) {
          vv_Str ck = vv_fmt(c, "c%d_%d", r, col);
          bool editing = (s->edit_r == r && s->edit_c == col);
          uint32_t cell = vv_box_keyed(
              c, ck, vv_str_len(ck),
              (vv_LayoutDecl){.w = vv_fixed(88), .h = vv_fixed(34),
                              .padding = editing ? vv_all(0) : vv_hv(6, 0),
                              .cross = VV_ALIGN_CENTER, .focusable = true, .clip = true},
              (vv_Style){.bg = TH->surface,
                         .border_width = editing ? vv_all(0) : vv_all(1),
                         .border_color = TH->border});
          {
            if (editing) {
              if (s->edit_fresh) { vv_request_focus_next(c); s->edit_fresh = false; }
              // Buffer is the model — edits mutate it directly, no message.
              vv_text_field(c, vv_fmt(c, "e%d_%d", r, col), s->cells[r][col], 32, "", VV_MSG_NONE);
            } else {
              vv_Str disp = s->cells[r][col][0] == '='
                                ? vv_fmt(c, "%.4g", eval_cell(s, r, col, 0))
                                : vv_fmt(c, "%s", s->cells[r][col]);
              vv_text(c, disp, (vv_Style){.fg = TH->text, .font_size = 13});
            }
          }
          vv_end_box(c);
          if (vv_clicked(c, cell)) vv_emit(c, MSG_CELL_EDIT, vv_pi(r * CELL_COLS + col));
        }
      }
    }
  }
}

// -------------------------------------------------------------------- shell
static const char *TASKS[] = {"Counter", "Temperature", "Flight Booker",
                              "Timer",   "CRUD",        "Circle Drawer", "Cells"};

static void view(vv_Ctx *c, void *state) {
  App *s = state;
  VV_BOX(c, ((vv_LayoutDecl){.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)}),
         ((vv_Style){.bg = vv_rgb(0.09f, 0.10f, 0.12f)})) {
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 4, .w = vv_fixed(170),
                               .h = vv_grow(1), .padding = vv_all(12)}),
           ((vv_Style){.bg = vv_rgb(0.12f, 0.13f, 0.16f)})) {
      vv_text(c, "7GUIs", (vv_Style){.fg = TH->text, .font_size = 20});
      vv_box_keyed(c, "sp", 1, (vv_LayoutDecl){.h = vv_fixed(8)}, (vv_Style){0});
      vv_end_box(c);
      for (int i = 0; i < 7; i++)
        vv_list_item(c, vv_fmt(c, "t%d", i), TASKS[i], s->task == i,
                     MSG_SELECT_TASK, vv_pi(i));
    }
    VV_BOX(c, ((vv_LayoutDecl){.dir = VV_COLUMN, .gap = 18, .w = vv_grow(1),
                               .h = vv_grow(1), .padding = vv_all(28)}),
           (vv_Style){0}) {
      switch (s->task) {
      case 0: task_counter(c, s); break;
      case 1: task_temp(c, s);    break;
      case 2: task_flight(c, s);  break;
      case 3: task_timer(c, s);   break;
      case 4: task_crud(c, s);    break;
      case 5: task_circle(c, s);  break;
      case 6: task_cells(c, s);   break;
      }
    }
  }
}

static void app_init(App *s) {
  *s = (App){0};
  s->duration = 12.0f;
  s->sel = -1;
  s->csel = -1;
  s->hist_n = 1;
  s->edit_r = s->edit_c = -1;
  strcpy(s->db[0].name, "Hans");   strcpy(s->db[0].surname, "Emil");
  strcpy(s->db[1].name, "Max");    strcpy(s->db[1].surname, "Mustermann");
  strcpy(s->db[2].name, "Roman");  strcpy(s->db[2].surname, "Tisch");
  s->db_n = 3;
  strcpy(s->d1, "27.03.2014");
  strcpy(s->d2, "27.03.2014");
}

int main(void) {
  vv_App *app = vv_app_create("Verve · 7GUIs", 900, 640);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx;
  vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  TH = vv_theme();

  App state; app_init(&state);

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
    prev = now;
    if (dt > 0.1f) dt = 0.1f;

    int w, h; float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    // Timer heartbeat: while it runs, emit a tick so the frame rebuilds and
    // advances. When it completes, ticks stop and the app falls idle.
    if (state.task == 3 && state.elapsed < state.duration)
      vv_emit(&ctx, MSG_TICK, vv_pf(dt));

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);

    vv_app_frame_begin(app, vv_rgb(0.09f, 0.10f, 0.12f));
    vv_render(vv_app_backend(app), cmds, w, h, dpi);
    vv_app_frame_end(app);
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
