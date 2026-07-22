// transitions.c — a gallery of state-driven transitions, all animated for free.
//
// The whole point: you never write animation code. You declare a *different tree
// or style* for the current state, and the retained tree's springs interpolate
// the difference. Three showcases, composed under one segmented control:
//
//   1. Segmented tabs — the selection pill FLIP-slides to the active segment,
//      and switching panels cross-fades (the old subtree exit-fades as the new
//      one enter-fades, both via the built-in lifecycle springs).
//   2. Async card — skeleton -> loaded -> empty -> error. Each state is just a
//      different subtree; the card reflows and cross-fades between them.
//   3. Toast stack — toasts fade in, auto-expire and exit, and the survivors
//      spring up to fill the gap. Time is fed in as MSG_TICK events (the Elm
//      way), so update() stays the only mutator.
//
// Build with `make gui`, run ./build/transitions.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define MAXTOAST 6
#define TOAST_LIFE 3.2f

enum {
  MSG_TAB = 1,     // payload: tab index
  MSG_ASYNC,       // payload: async state
  MSG_ADD_TOAST,   // append a toast (kind rotates)
  MSG_DISMISS,     // payload: toast id
  MSG_TICK,        // payload: dt — time as an input event
};

typedef struct { int id, kind; float age; char msg[40]; } Toast;

typedef struct {
  int   tab;         // 0 = async, 1 = toasts
  int   async;       // 0 loading, 1 loaded, 2 empty, 3 error
  Toast toasts[MAXTOAST];
  int   ntoast, next_id, kind_rot;
} App;

static void toast_remove(App *a, int i) {
  memmove(&a->toasts[i], &a->toasts[i + 1],
          (size_t)(a->ntoast - i - 1) * sizeof(Toast));
  a->ntoast--;
}

static void update(void *st, vv_Event ev) {
  App *a = st;
  switch (ev.msg) {
  case MSG_TAB:   a->tab = (int)ev.data.as_int; break;
  case MSG_ASYNC: a->async = (int)ev.data.as_int; break;
  case MSG_ADD_TOAST:
    if (a->ntoast < MAXTOAST) {
      static const char *M[3] = {"Saved to disk", "Low battery", "Upload failed"};
      int kind = a->kind_rot++ % 3;
      Toast *t = &a->toasts[a->ntoast++];
      t->id = ++a->next_id; t->kind = kind; t->age = 0;
      snprintf(t->msg, sizeof t->msg, "%s", M[kind]);
    }
    break;
  case MSG_DISMISS:
    for (int i = 0; i < a->ntoast; i++)
      if (a->toasts[i].id == (int)ev.data.as_int) { toast_remove(a, i); break; }
    break;
  case MSG_TICK: {
    float dt = (float)ev.data.as_float;
    for (int i = a->ntoast - 1; i >= 0; i--) {
      a->toasts[i].age += dt;
      if (a->toasts[i].age > TOAST_LIFE) toast_remove(a, i); // -> exit-fades
    }
    break;
  }
  default: break;
  }
}

// A sliding-pill segmented control. The indicator is one absolutely-positioned
// box whose target x jumps to the active segment; its FLIP rx spring slides it.
static void segmented(vv_Ctx *c, const char *key, const char *const *labels,
                      int n, int active, vv_Msg msg) {
  const vv_Theme *t = vv_theme();
  const float segW = 116, H = 40, pad = 4;
  vv_box_keyed(c, key, strlen(key),
               VV_LAYOUT(.dir = VV_ROW, .w = vv_fixed(segW * n), .h = vv_fixed(H),
                         .padding = vv_all(pad)),
               VV_STYLE(.bg = t->surface, .radius = vv_r(11)));
  {
    // indicator behind the labels (built first => drawn under)
    vv_box_keyed(c, "ind", 3,
                 VV_LAYOUT(.has_absolute = true,
                           .absolute = vv_rect(active * segW, 0, segW, H - 2 * pad)),
                 VV_STYLE(.bg = t->accent, .radius = vv_r(8)));
    vv_end_box(c);
    for (int i = 0; i < n; i++) {
      char k[8]; snprintf(k, sizeof k, "s%d", i);
      uint32_t seg = vv_box_keyed(
          c, k, strlen(k),
          VV_LAYOUT(.w = vv_fixed(segW), .h = vv_grow(1), .main = VV_ALIGN_CENTER,
                    .cross = VV_ALIGN_CENTER),
          VV_STYLE(.bg = {0}));
      vv_text(c, labels[i],
              VV_STYLE(.fg = i == active ? t->on_accent : t->text_muted,
                       .font_size = 15));
      vv_end_box(c);
      if (vv_clicked(c, seg)) vv_emit(c, msg, vv_pi(i));
    }
  }
  vv_end_box(c);
}

// ---- panel 1: async states ----
static void skeleton_bar(vv_Ctx *c, const char *k, float w) {
  vv_box_keyed(c, k, strlen(k),
               VV_LAYOUT(.w = vv_percent(w), .h = vv_fixed(16)),
               VV_STYLE(.bg = vv_rgb(0.20f, 0.22f, 0.26f), .radius = vv_r(5)));
  vv_end_box(c);
}

static void async_panel(vv_Ctx *c, App *a) {
  const vv_Theme *t = vv_theme();
  static const char *S[4] = {"Loading", "Loaded", "Empty", "Error"};
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 16, .cross = VV_ALIGN_START),
         VV_STYLE(.bg = {0})) {
    segmented(c, "asy", S, 4, a->async, MSG_ASYNC);

    // The card: one container that persists across states (so it reflows), with
    // a state-specific subtree inside that enter/exit-fades on change.
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(420), .gap = 12,
                        .padding = vv_all(20), .main = VV_ALIGN_CENTER),
           VV_STYLE(.bg = t->surface, .radius = vv_r(14),
                    .border_width = vv_all(1), .border_color = t->border)) {
      if (a->async == 0) { // loading skeleton
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN,/* .w = vv_grow(1)*/ .gap = 12),
               VV_STYLE(.bg = {0})) {
          skeleton_bar(c, "sk0", 60);
          skeleton_bar(c, "sk1", 100);
          skeleton_bar(c, "sk2", 85);
        }
      } else if (a->async == 1) { // loaded
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 8),
               VV_STYLE(.bg = {0})) {
          vv_text(c, "Quarterly report", VV_STYLE(.fg = t->text, .font_size = 20));
          vv_text(c, "Revenue up 12% over last quarter, driven by\nthe new pricing tier.",
                  VV_STYLE(.fg = t->text_muted, .font_size = 14));
          vv_button(c, "open", "Open", MSG_TAB, vv_pi(a->tab));
        }
      } else if (a->async == 2) { // empty
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 6,
                            .cross = VV_ALIGN_CENTER, .main = VV_ALIGN_CENTER),
               VV_STYLE(.bg = {0})) {
          vv_text(c, "\xe2\x97\x8b", VV_STYLE(.fg = t->text_muted, .font_size = 34));
          vv_text(c, "Nothing here yet", VV_STYLE(.fg = t->text_muted, .font_size = 15));
        }
      } else { // error
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 8,
                            .padding = vv_all(12)),
               VV_STYLE(.bg = vv_rgba(0.90f, 0.35f, 0.30f, 0.12f), .radius = vv_r(10))) {
          vv_text(c, "\xe2\x9a\xa0  Couldn't load", VV_STYLE(.fg = t->danger, .font_size = 17));
          vv_text(c, "The server returned 503. Try again in a moment.",
                  VV_STYLE(.fg = t->text_muted, .font_size = 14));
          vv_button(c, "retry", "Retry", MSG_ASYNC, vv_pi(0));
        }
      }
    }
  }
}

// ---- panel 2: toast stack ----
static void toast_panel(vv_Ctx *c, App *a) {
  const vv_Theme *t = vv_theme();
  vv_Color kindc[3] = {vv_rgb(0.35f, 0.75f, 0.4f), vv_rgb(0.95f, 0.7f, 0.25f), t->danger};
  const char *icon[3] = {"\xe2\x9c\x93", "\xe2\x9a\xa0", "\xe2\x9c\x95"};

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 14, .cross = VV_ALIGN_START),
         VV_STYLE(.bg = {0})) {
    vv_button(c, "add", "Add toast", MSG_ADD_TOAST, VV_NO_PAYLOAD);
    vv_text(c, "auto-expires after 3s · click one to dismiss now",
            VV_STYLE(.fg = t->text_muted, .font_size = 13));

    // The stack. Each toast keyed by id, so identity survives removals: when one
    // expires it exit-fades and the rest FLIP-spring up to close the gap.
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(320), .gap = 10),
           VV_STYLE(.bg = {0})) {
      for (int i = 0; i < a->ntoast; i++) {
        Toast *ts = &a->toasts[i];
        char k[12]; snprintf(k, sizeof k, "t%d", ts->id);
        uint32_t card = vv_box_keyed(
            c, k, strlen(k),
            VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                      .gap = 10, .padding = vv_hv(14, 12)),
            VV_STYLE(.bg = t->surface_hi, .radius = vv_r(10),
                     .border_width = (vv_Edges){3, 0, 0, 0},
                     .border_color = kindc[ts->kind],
                     .shadow = {.color = vv_rgba(0, 0, 0, 0.35f),
                                .offset = {0, 4}, .blur = 16}));
        vv_text(c, icon[ts->kind], VV_STYLE(.fg = kindc[ts->kind], .font_size = 16));
        vv_text(c, ts->msg, VV_STYLE(.fg = t->text, .font_size = 14));
        vv_end_box(c);
        if (vv_clicked(c, card)) vv_emit(c, MSG_DISMISS, vv_pi(ts->id));
      }
    }
  }
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  static const char *TABS[2] = {"Async", "Toasts"};

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .padding = vv_all(28), .gap = 22),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.10f, 0.12f))) {
    vv_text(c, "Transitions", VV_STYLE(.fg = t->text, .font_size = 26));
    segmented(c, "tabs", TABS, 2, a->tab, MSG_TAB);

    // Panel area. Only the active panel is built; switching swaps the subtree,
    // so the old one exit-fades while the new one enter-fades (a cross-fade).
    VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1), .padding = vv_all(4)),
           VV_STYLE(.bg = {0})) {
      if (a->tab == 0) async_panel(c, a);
      else             toast_panel(c, a);
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Transitions", 640, 560);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);

  App state = {.async = 1};
  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    // Feed time in as an event while toasts are alive, so update() can expire
    // them without the host mutating state behind its back. Keeps frames coming.
    if (state.ntoast > 0) { vv_emit(&ctx, MSG_TICK, vv_pf((double)dt)); vv_invalidate(&ctx); }

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.09f, 0.10f, 0.12f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
