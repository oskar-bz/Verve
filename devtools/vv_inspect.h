// vv_inspect.h — a drop-in DevTools-style inspector for any Verve app.
//
// Single header. In exactly one .c file define the implementation:
//     #define VV_INSPECT_IMPL
//     #include "vv_inspect.h"
// elsewhere just #include it. Depends only on the Verve core (verve.h), not on
// any backend, so it attaches to whatever window/loop you already have.
//
// It reads your app's retained pool read-only and paints itself as an overlay
// on the right edge — your app keeps its full window and stays interactive.
// Because a tree lives in process memory, this is compile-in attachment (an
// overlay in the same process), not attach-to-a-running-process.
//
// ------------------------------------------------------------------ attaching
//   #define VV_INSPECT_IMPL
//   #include "vv_inspect.h"
//   ...
//   vv_Ctx ctx; vv_init(&ctx);
//   vv_set_measure_fn(&ctx, my_measure, my_ud);
//
//   vv_Inspector ins;
//   vv_inspect_init(&ins, &ctx, my_measure, my_ud);
//
//   // per frame (w,h,dpi from your window; `in` is your vv_Input):
//   vv_Input app_in = vv_inspect_split(&ins, in, w, h); // routes the pointer
//   vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &app_in, update, view, &state);
//   vv_CommandBuffer *ov   = vv_inspect_render(&ins, dt, w, h, dpi);
//
//   frame_begin(clear);
//   if (cmds) render(cmds);   // your app
//   if (ov)   render(ov);     // the inspector, on top
//   frame_end();
//
//   // toggle however your backend exposes keys, e.g. on F12:
//   vv_inspect_toggle(&ins);
#ifndef VV_INSPECT_H
#define VV_INSPECT_H

#include "verve/verve.h"

#ifndef VV_MEASUREFN_DEFINED
#define VV_MEASUREFN_DEFINED
typedef vv_Vec2 (*vv_MeasureFn)(void *ud, const char *s, int len, vv_FontID font,
                                float size, float wrap_width);
#endif

typedef struct {
  vv_Ctx  ctx;        // the inspector's own context (separate tree)
  vv_Ctx *target;     // the app under inspection
  vv_ID   selected;   // node pinned in the panel (0 = none)
  vv_ID   hovered;    // node under the cursor in the app
  bool    open;
  float   panel_w;

  // scratch carried between split() and render() within a frame
  vv_Input ui_in;
  bool     over_panel;
} vv_Inspector;

// Attach to `target`. `measure`/`ud` are the same text-measurement callback you
// gave the app (the inspector needs it to lay out its own text).
void vv_inspect_init(vv_Inspector *ins, vv_Ctx *target, vv_MeasureFn measure,
                     void *ud);

void vv_inspect_toggle(vv_Inspector *ins);

// Call once per frame BEFORE running your app. Splits the pointer between the
// app and the panel and returns the vv_Input your app should use. When open it
// also keeps the app rebuilding so the overlay composites cleanly.
vv_Input vv_inspect_split(vv_Inspector *ins, vv_Input raw, float w, float h);

// Call AFTER running your app's frame. Builds the overlay and returns its
// command buffer (NULL when closed). Render it on top of your app's buffer.
vv_CommandBuffer *vv_inspect_render(vv_Inspector *ins, float dt, float w,
                                    float h, float dpi);

#endif // VV_INSPECT_H

// ============================================================================
#ifdef VV_INSPECT_IMPL
#include <stdio.h>
#include <string.h>

static const char *vvi_size_mode(vv_SizeMode m) {
  switch (m) {
  case VV_SIZE_FIT:     return "fit";
  case VV_SIZE_FIXED:   return "fixed";
  case VV_SIZE_GROW:    return "grow";
  case VV_SIZE_PERCENT: return "percent";
  }
  return "?";
}

static void vvi_prop(vv_Ctx *c, const char *label, const char *val) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 8,
                      .main = VV_ALIGN_SPACE_BETWEEN),
         VV_STYLE(.bg = {0})) {
    vv_text(c, label, VV_STYLE(.fg = t->text_muted, .font_size = 13));
    vv_text(c, val, VV_STYLE(.fg = t->text, .font_size = 13));
  }
}

static void vvi_hex(char *buf, size_t n, vv_Color c) {
  snprintf(buf, n, "#%02X%02X%02X %.0f%%", (int)(c.r * 255 + 0.5f),
           (int)(c.g * 255 + 0.5f), (int)(c.b * 255 + 0.5f), c.a * 100);
}

static void vvi_tree_rows(vv_Ctx *c, vv_Inspector *ins, uint32_t idx, int depth) {
  const vv_Theme *t = vv_theme();
  vv_Node *n = vv_pool_get(&ins->target->pool, idx);
  if (!n || (n->flags & VV_FLAG_EXITING)) return;

  bool sel = n->id == ins->selected, hov = n->id == ins->hovered;
  bool text = (n->flags & VV_FLAG_TEXT) != 0;
  char label[80];
  if (text)
    snprintf(label, sizeof label, "%.*s",
             n->text_len > 40 ? 40 : (int)n->text_len, n->text ? n->text : "");
  else
    snprintf(label, sizeof label, "\xe2\x96\xa1 box  (%u)", n->child_count);

  char rk[16]; snprintf(rk, sizeof rk, "r%u", idx);
  uint32_t row = vv_box_keyed(
      c, rk, strlen(rk),
      VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                .padding = (vv_Edges){8 + depth * 14.0f, 4, 8, 4}),
      VV_STYLE(.bg = sel ? t->accent_lo : (hov ? t->surface_hi : (vv_Color){0}),
               .radius = vv_r(5)));
  vv_text(c, label, VV_STYLE(.fg = sel ? t->on_accent : (text ? t->text_muted : t->text),
                             .font_size = 13));
  vv_end_box(c);
  if (vv_clicked(c, row)) ins->selected = n->id;
  if (vv_hovered(c, row)) ins->hovered = n->id;

  for (uint32_t ch = n->first_child; ch != VV_NIL;) {
    vv_Node *cn = vv_pool_get(&ins->target->pool, ch);
    vvi_tree_rows(c, ins, ch, depth + 1);
    ch = cn->next_sibling;
  }
}

static void vvi_properties(vv_Ctx *c, vv_Inspector *ins) {
  const vv_Theme *t = vv_theme();
  uint32_t idx = vv_pool_find(&ins->target->pool, ins->selected);
  if (idx == VV_NIL) {
    vv_text(c, "select a node", VV_STYLE(.fg = t->text_muted, .font_size = 13));
    return;
  }
  vv_Node *n = vv_pool_get(&ins->target->pool, idx);
  char b[96];

  vv_text(c, "RECT (layout \xe2\x86\x92 actual)",
          VV_STYLE(.fg = t->accent_hi, .font_size = 12));
  snprintf(b, sizeof b, "%.0f,%.0f  %.0f\xc3\x97%.0f", n->layout_rect.x,
           n->layout_rect.y, n->layout_rect.w, n->layout_rect.h);
  vvi_prop(c, "target", b);
  snprintf(b, sizeof b, "%.1f,%.1f  %.1f\xc3\x97%.1f", n->actual_rect.x,
           n->actual_rect.y, n->actual_rect.w, n->actual_rect.h);
  vvi_prop(c, "actual", b);
  bool settled = n->rx.v == 0 && n->ry.v == 0 && n->rw.v == 0 && n->rh.v == 0 &&
                 n->rx.x == n->rx.target && n->ry.x == n->ry.target;
  vvi_prop(c, "state", settled ? "settled" : "springing");
  snprintf(b, sizeof b, "%.1f, %.1f", n->rx.v, n->ry.v);
  vvi_prop(c, "vel x,y", b);

  vv_text(c, "LAYOUT", VV_STYLE(.fg = t->accent_hi, .font_size = 12));
  vvi_prop(c, "dir", n->decl.dir == VV_ROW ? "row" : "column");
  snprintf(b, sizeof b, "%s %.0f", vvi_size_mode(n->decl.w.mode), n->decl.w.value);
  vvi_prop(c, "width", b);
  snprintf(b, sizeof b, "%s %.0f", vvi_size_mode(n->decl.h.mode), n->decl.h.value);
  vvi_prop(c, "height", b);
  snprintf(b, sizeof b, "%.0f %.0f %.0f %.0f", n->decl.padding.l, n->decl.padding.t,
           n->decl.padding.r, n->decl.padding.b);
  vvi_prop(c, "padding", b);
  snprintf(b, sizeof b, "%.0f", n->decl.gap);
  vvi_prop(c, "gap", b);
  snprintf(b, sizeof b, "%.0f / %.0f", n->fit_w, n->fit_h);
  vvi_prop(c, "fit w/h", b);

  vv_text(c, "STYLE", VV_STYLE(.fg = t->accent_hi, .font_size = 12));
  vvi_hex(b, sizeof b, n->target.bg); vvi_prop(c, "bg", b);
  vvi_hex(b, sizeof b, n->target.fg); vvi_prop(c, "fg", b);
  snprintf(b, sizeof b, "%.0f", n->target.radius.tl);
  vvi_prop(c, "radius", b);

  vv_text(c, "FLAGS", VV_STYLE(.fg = t->accent_hi, .font_size = 12));
  snprintf(b, sizeof b, "%s%s%s%s",
           n->flags & VV_FLAG_HOVERED ? "hover " : "",
           n->flags & VV_FLAG_ACTIVE ? "active " : "",
           n->flags & VV_FLAG_FOCUSED ? "focus " : "",
           n->flags & VV_FLAG_TEXT ? "text" : "");
  vvi_prop(c, "set", b[0] ? b : "\xe2\x80\x94");
}

static void vvi_view(vv_Ctx *c, void *st) {
  vv_Inspector *ins = st;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1)), VV_STYLE(.bg = {0})) {
    // selected-node highlight over the app canvas (absolute = window coords)
    uint32_t si = vv_pool_find(&ins->target->pool, ins->selected);
    if (si != VV_NIL) {
      vv_Rect r = vv_pool_get(&ins->target->pool, si)->actual_rect;
      vv_box_keyed(c, "hi", 2,
                   VV_LAYOUT(.has_absolute = true, .absolute = r),
                   VV_STYLE(.bg = vv_rgba(0.22f, 0.55f, 0.95f, 0.12f),
                            .border_width = vv_all(2), .border_color = t->accent_hi));
      vv_end_box(c);
    }
    uint32_t hi = vv_pool_find(&ins->target->pool, ins->hovered);
    if (hi != VV_NIL && ins->hovered != ins->selected) {
      vv_Rect r = vv_pool_get(&ins->target->pool, hi)->actual_rect;
      vv_box_keyed(c, "hh", 2, VV_LAYOUT(.has_absolute = true, .absolute = r),
                   VV_STYLE(.border_width = vv_all(1),
                            .border_color = vv_rgba(1, 1, 1, 0.35f)));
      vv_end_box(c);
    }

    // the panel, pinned to the right edge
    vv_box_keyed(c, "panel", 5,
                 VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(ins->panel_w),
                           .has_absolute = true,
                           .absolute = vv_rect(c->win_w - ins->panel_w, 0,
                                               ins->panel_w, c->win_h),
                           .padding = vv_all(14), .gap = 10),
                 VV_STYLE(.bg = vv_rgb(0.07f, 0.08f, 0.10f),
                          .border_width = (vv_Edges){1, 0, 0, 0},
                          .border_color = t->border));
    {
      vv_text(c, "Inspector", VV_STYLE(.fg = t->text, .font_size = 18));
      vv_text(c, "hover/click the app or a row",
              VV_STYLE(.fg = t->text_muted, .font_size = 12));
      vv_box_keyed(c, "tree", 4,
                   VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                             .scroll_y = true, .clip = true, .gap = 1,
                             .padding = vv_all(4)),
                   VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.14f), .radius = vv_r(8)));
      vvi_tree_rows(c, ins, ins->target->root, 0);
      vv_end_box(c);
      vv_box_keyed(c, "props", 5,
                   VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_fixed(300),
                             .scroll_y = true, .clip = true, .gap = 5,
                             .padding = vv_all(10)),
                   VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.14f), .radius = vv_r(8)));
      vvi_properties(c, ins);
      vv_end_box(c);
    }
    vv_end_box(c);
  }
}

void vv_inspect_init(vv_Inspector *ins, vv_Ctx *target, vv_MeasureFn measure,
                     void *ud) {
  memset(ins, 0, sizeof *ins);
  ins->target = target;
  ins->open = true;
  ins->panel_w = 380.0f;
  vv_init(&ins->ctx);
  vv_set_measure_fn(&ins->ctx, measure, ud);
}

void vv_inspect_toggle(vv_Inspector *ins) { ins->open = !ins->open; }

vv_Input vv_inspect_split(vv_Inspector *ins, vv_Input raw, float w, float h) {
  (void)h;
  if (!ins->open) { ins->over_panel = false; return raw; }

  float split = w - ins->panel_w;
  ins->over_panel = raw.mouse.x >= split;

  vv_Input app_in = raw, ui_in = raw;
  if (ins->over_panel) { app_in.mouse_down = false; app_in.mouse = vv_v2(-1, -1); }
  else                 { ui_in.mouse_down = false;  ui_in.mouse = vv_v2(-1, -1); }
  ins->ui_in = ui_in;

  // Keep the app rebuilding so the overlay always has fresh geometry to draw
  // over (and so an idle-mode app doesn't blank under a cleared framebuffer).
  vv_invalidate(ins->target);
  return app_in;
}

vv_CommandBuffer *vv_inspect_render(vv_Inspector *ins, float dt, float w,
                                    float h, float dpi) {
  if (!ins->open) return NULL;

  // Mirror the app's hover; select on press inside the app region.
  ins->hovered = ins->target->hovered_id;
  if (!ins->over_panel && ins->target->pressed_id)
    ins->selected = ins->target->pressed_id;

  vv_set_window(&ins->ctx, w, h, dpi);
  vv_invalidate(&ins->ctx); // rebuild every frame to track the app
  return vv_run_frame(&ins->ctx, dt, &ins->ui_in, NULL, vvi_view, ins);
}

#endif // VV_INSPECT_IMPL
