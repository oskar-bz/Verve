# Verve — Usage Guide

Verve is an animation-native, immediate-mode UI library in C. You describe the
UI you *want* every frame; Verve keeps a retained tree behind the scenes and
springs the on-screen result toward your description. Nothing you draw ever
"snaps" unless you ask it to.

This guide is task-oriented. For the design rationale see
[verve-design.md](verve-design.md); for the exact signatures read the headers in
`include/verve/`.

---

## 1. The mental model

Three ideas carry the whole library:

1. **Immediate API, retained state.** Your `view` function runs top-to-bottom
   and *declares* boxes, text, and widgets. Verve reconciles that declaration
   against a retained node tree using stable identity, so a widget keeps its
   animation and internal state across frames even though you "rebuild" it every
   time.

2. **Target vs. actual.** Your style/layout is the *target*. Each node also
   holds an *actual* value plus velocity. In the Present phase springs move
   actual toward target. Change a color, a size, or a whole theme and it
   animates for free.

3. **Message / update / view (The Elm Architecture).** Widgets never return
   "was clicked". They **emit a message** into a queue. A driver drains the
   queue into your `update(state, event)`, then re-runs `view(ctx, state)` only
   when something changed.

   ```
   view()  emits  ->  [ queue ]  ->  drained into  update()  ->  mutates state
     ^                                                                  |
     +---------------------- re-run when state changed -----------------+
   ```

   `view` is pure: it reads state and emits, never mutates. `update` is the only
   place state changes and never touches the UI tree. This split is the reason
   the code stays testable.

---

## 2. A complete application

Every app is: define **messages**, define **state**, write **update**, write
**view**, then spin the loop. Here is the reference counter (see
`examples/mycounter.c`):

```c
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>

// 1. Messages — user-defined ids. 0 is reserved (VV_MSG_NONE), so start at 1.
enum { MSG_STEP = 1, MSG_TOGGLE_SUB, MSG_RESET };

// 2. State.
typedef struct { bool subtract; int64_t counter; } Counter;

// 3. update — the ONLY place state mutates.
static void update(void *state, vv_Event ev) {
  Counter *s = state;
  switch (ev.msg) {
  case MSG_STEP:       s->counter += ev.data.as_int; break;
  case MSG_TOGGLE_SUB: s->subtract = ev.data.as_int; break;
  case MSG_RESET:      s->counter  = 0;              break;
  }
}

// 4. view — pure function of state; emits messages, never mutates.
static void view(vv_Ctx *c, void *state) {
  Counter *s = state;
  const vv_Theme *t = vv_theme();
  int64_t step = s->subtract ? -1 : 1;

  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER, .gap = 12),
         VV_STYLE(.bg = vv_rgb(0.09f, 0.09f, 0.09f))) {

    vv_text(c, "Counter!", VV_STYLE(.fg = t->text, .font_size = 26));
    vv_checkbox(c, "sub", "Enable subtraction?", s->subtract, MSG_TOGGLE_SUB);
    vv_text(c, vv_fmt(c, "%lld", (long long)s->counter),
            VV_STYLE(.fg = t->text, .font_size = 40));

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .gap = 10), VV_STYLE(.bg = {0})) {
      vv_button(c, "step1",  s->subtract ? "-1"  : "+1",  MSG_STEP, vv_pi(step));
      vv_button(c, "step10", s->subtract ? "-10" : "+10", MSG_STEP, vv_pi(step*10));
      vv_button(c, "reset",  "Reset", MSG_RESET, VV_NO_PAYLOAD);
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve · Counter", 900, 640);
  vv_app_load_font(app, "/usr/share/fonts/noto/NotoSans-Regular.ttf");

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app); // text metrics for layout
  vv_set_idle_mode(&ctx, true);                 // ~0% CPU when nothing moves

  Counter state = {0};
  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();

  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    // The whole loop: drain messages -> update -> conditionally rebuild view.
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &state);

    if (cmds) { // NULL => fully idle this frame; skip draw + swap.
      vv_app_frame_begin(app, vv_rgb(0.09f, 0.09f, 0.09f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
}
```

`vv_run_frame` is the blessed driver. It processes last frame's messages, then
rebuilds the view only if a message fired or this frame's input could produce
one — otherwise it just *presents* (advances springs). With idle mode on, a
fully-settled UI returns `NULL` so you skip the draw entirely.

---

## 3. Layout

Layout is a flexbox-style solver. A box has a direction, per-axis sizing,
padding, gap, and alignment. Fields are zero-init friendly — omit what you don't
need.

```c
VV_BOX(c, VV_LAYOUT(
    .dir     = VV_ROW,          // or VV_COLUMN
    .w       = vv_grow(1),      // sizing (below); .h defaults to FIT
    .padding = {12,12,12,12},   // l, t, r, b
    .gap     = 8,
    .main    = VV_ALIGN_CENTER, // along .dir
    .cross   = VV_ALIGN_CENTER, // perpendicular
  ), VV_STYLE(.bg = t->surface)) {
    /* children */
}
```

Sizing modes (per axis, `.w` / `.h`):

| Constructor | Meaning |
|---|---|
| `vv_fit()`      | intrinsic content size (default when zero-init) |
| `vv_fixed(n)`   | exactly `n` logical pixels |
| `vv_grow(w)`    | share of leftover space, weight `w` |
| `vv_percent(p)` | fraction `p` of the parent's resolved size |

Alignment values: `VV_ALIGN_START`, `_CENTER`, `_END`, and `_SPACE_BETWEEN`
(main axis only). Other useful `VV_LAYOUT` fields: `.wrap`, `.clip`,
`.scroll_x/.scroll_y`, `.focusable`, `.disabled`, `.absolute` + `.has_absolute`
(escape flow for popovers/tooltips), `.z` (layer), `.aspect_ratio`.

A `.scroll_x/.scroll_y` box clips its overflow, glides on a spring, and draws an
overlay scrollbar thumb automatically when the content overflows that axis — you
don't add a scrollbar widget. Pair it with `vv_rows` (below) to virtualize long
lists so only the visible rows are built.

`VV_BOX(...) { }` is a scoped macro — the closing brace emits `vv_end_box`. If
you need the box handle (for queries or `vv_on`), call the function form:

```c
uint32_t id = vv_box(c, decl, style);
/* ...children via vv_box_keyed / vv_text... */
vv_end_box(c);
```

---

## 4. Styling and animation

`VV_STYLE(...)` is a sparse overlay: zero-init means "inherit from theme". Set
only the channels you care about.

```c
VV_STYLE(
  .bg           = t->surface,
  .fg           = t->text,          // text color
  .radius       = {8,8,8,8},        // per corner
  .border_width = {1,1,1,1},        // per side
  .border_color = t->border,
  .opacity      = 1.0f,
  .font_size    = 16,
)
```

**Everything animates automatically.** Because the actual value springs toward
the target, simply declaring a different `.bg` this frame produces a smooth
transition. State variants attach hover/active/focus/disabled overlays that also
spring:

```c
static const vv_Style hover = VV_STYLE(.bg = /*...*/, .set = VV_STYLE_BG);
vv_box(c, decl, VV_STYLE(.bg = base, .hover = &hover));
```

(Variants need the `.set` presence mask so "transparent black" is
distinguishable from "unset".)

When a value is *continuously driven* every frame (a progress bar width, an
audio meter), springing would make it lag reality. Opt that channel out:

```c
VV_STYLE(.transition_mask = VV_INSTANT_RECT)  // rect tracks reality, no FLIP
```

### Themes

A `vv_Theme` is just a bag of colors + a default font/size. Swapping the whole
theme animates every widget at once for free.

```c
const vv_Theme *t = vv_theme();       // current
vv_set_theme(&my_theme);              // triggers a global animated transition
vv_Theme base = vv_theme_dark();      // sensible starting palette
```

---

## 5. Built-in widgets

Widgets are **emit-only**: they push a message on interaction and return their
node handle (for `vv_state` or interaction queries). The `key` argument gives
stable identity inside loops/conditionals — pass `NULL` to fall back on sequence
order.

```c
vv_button(c, "ok", "Save", MSG_SAVE, VV_NO_PAYLOAD);       // click -> MSG_SAVE
vv_checkbox(c, "a", "Wrap", wrap, MSG_WRAP);               // emits !value
vv_toggle(c, "t", enabled, MSG_ENABLE);
vv_slider(c, "vol", vol, 0.f, 1.f, MSG_VOL);               // emits new float
vv_drag_number(c, "x", x, /*speed*/0.5f, -100, 100, MSG_X);
vv_list_item(c, "row3", "Item 3", selected, MSG_PICK, vv_pi(3));
vv_text_field(c, "name", buf, sizeof buf, "Name…", MSG_NAME); // edits buf in place
vv_text_area(c, "body", buf, sizeof buf, /*height*/0, NULL, MSG_BODY); // multi-line
vv_date_field(c, "day", packed_date, MSG_DAY);            // calendar; emits packed date
vv_label(c, "Plain");  vv_label_muted(c, "Secondary");
```

More catalogue widgets, same shape (emit a message, return a handle):

```c
vv_radio(c, "r1", "Medium", val == 1, MSG_LEVEL, vv_pi(1));   // group via arg
vv_progress(c, "p", 0.4f);                                    // determinate bar
vv_stepper(c, "freq", freq, 10, 0, 100, "Hz", MSG_FREQ);     // [-] value unit [+]
vv_tabs(c, "t", (const char*[]){"A","B"}, 2, tab, MSG_TAB);  // sliding indicator
vv_combobox(c, "sel", opts, n, cur, MSG_SEL);                // dropdown, self-open
if (vv_tree_item(c, id, name, depth, leaf, expanded, sel)) { /* toggle/select */ }
```

Larger stateful widgets live alongside these — `vv_splitter` (resizable panels),
`vv_menubar`/`vv_menu_*`, `vv_popover_*`, `vv_tooltip`, `vv_date_field`. They're
covered under **Desktop features** below; the point is they're all just functions
in the same public API, with no privileged access.

### Right-click, cursor shapes, clipboard

`vv_right_clicked(c, id)` reports a right-button click — pair it with
`vv_context_menu_begin/…item…/end` (an overlay that dismisses on outside-click)
for context menus. A node can request a pointer shape via `.cursor` in its
layout (`VV_CURSOR_TEXT`, `VV_CURSOR_RESIZE_H`, …); the host applies it each
frame with `vv_app_set_cursor(app, vv_cursor(&ctx))`. The built-in widgets
already set sensible cursors (text fields, splitters, buttons). The editors do
Ctrl-C/V/X once you bind the OS clipboard: `vv_app_bind_clipboard(app, &ctx)` at
startup (routes `vv_clipboard_get/set` through the backend).

Controlled widgets (checkbox/toggle/slider) take the *current* value to render
and emit the *new* value in the payload — your `update` writes it back. Read the
payload by kind: `ev.data.as_int` / `.as_float` / `.as_ptr` / `.as_str`.

### Payloads

A message carries a small union. Construct terse ones with `vv_pi` (int),
`vv_pf` (float), `vv_pp` (ptr), `vv_ps` (const char\*), or `VV_NO_PAYLOAD`. A 2D
point packs by value with `vv_pv2(vec)` / `vv_as_v2(payload)` — used by move
events so a cursor position needs no allocation.

### Extra interaction bindings

Every interactive widget has an `_on` variant taking a `vv_On` for
hover/press/double-click/move messages. You can also attach these to *any* plain
box with `vv_on`:

```c
uint32_t card = vv_box(c, decl, style);
/* build children */
vv_end_box(c);
vv_on(c, card, (vv_On){ .hover = MSG_HOVER_CARD, .dbl = MSG_OPEN });
```

`.move` is special: it's opt-in and it's the *only* thing that makes the view
rebuild on plain pointer motion (the payload is the cursor position). Leave it
unset and mouse movement that doesn't change hover/focus won't cost a rebuild.

---

## 6. Value bindings (two-way, no update case)

Sometimes a control just edits a variable and a full message round-trip is
ceremony. Wrap the variable in a `vv_Value` and use a `_bound` widget: it reads
the current value to render and, on change, emits the reserved `VV_MSG_BIND`
which the driver applies through the pointer *before* `update` runs. No `update`
case needed.

```c
float gain = 0.5f;
static const vv_ValueMeta gain_meta = { .name="Gain", .min=0, .max=1, .curve=2.0f };

vv_slider_bound(c, "gain", vv_f32(&gain, &gain_meta));   // honors min/max/curve
vv_drag_number_bound(c, "x", vv_f32(&x, NULL), 0.25f);
vv_checkbox_bound(c, "mute", "Mute", vv_boolval(&muted, NULL));
```

`vv_ValueMeta` carries `min`/`max` and a perceptual `curve` (1 = linear, >1 =
finer control near min). Drag-bound widgets bracket a whole drag as one edit
(`vv_begin_edit`/`vv_end_edit`, bumping `edit_generation`) so it's a single undo
step, not one per frame. `VV_VAL_READONLY` in the flags makes a widget display
but not write.

Use messages when a change has consequences (recompute, network, navigation);
use bindings for plain "edit this number" knobs.

---

## 7. Strings

Verve ships fat-pointer strings (`vv_Str` is a NUL-terminated `char*` with a
length/capacity header before the data — so it's a drop-in `const char*`). They
allocate from an arena; the point of the exercise is `str_format` plus the usual
operations:

```c
vv_Str s = vv_str_format(arena, "%d items, %.1f%%", n, pct);
size_t  n_len = vv_str_len(s);                 // O(1)
vv_Str *parts = vv_str_split(arena, csv, ',', &count);
vv_Str joined = vv_str_join(arena, items, count, ", ");
bool   ok = vv_str_starts_with(s, "http");
```

Also available: `vv_str_dup/from/cat/sub/trim/lower/upper`, `vv_str_eq`,
`vv_str_ends_with`, `vv_str_find`, `vv_str_contains`.

Inside `view`, the shortcut is `vv_fmt(c, fmt, ...)`: it formats into the frame
arena, valid until the next frame — long enough for the text widget to copy it —
so labels need no scratch buffer:

```c
vv_text(c, vv_fmt(c, "%.1f s", elapsed), VV_STYLE(.fg = t->text));
```

---

## 8. Writing a custom widget

There is no widget base class or registry — a widget is just a function using
the public API. The built-ins are the acceptance test for that API: if a
built-in needed private access, the API would be wrong. Two ingredients you'll
reach for:

- **`vv_state(ctx, id, T)`** — persistent, zeroed-on-first-use storage attached
  to a node, freed when the node dies. This is the one privileged call, for
  widgets that need memory between frames (a text field's cursor, a fold's open
  flag).
- **Interaction queries** — `vv_hovered/pressed/clicked/active/focused/`
  `vv_double_clicked(ctx, id)` and `vv_drag_delta(ctx, id)`. These reflect the
  hit test run at `begin_frame` (current pointer vs. *last* frame's geometry —
  the deliberate one-frame lag).

A minimal expandable section that emits a message when toggled and animates its
own chevron via style targets:

```c
// Returns whether it is open (persisted in node state).
bool my_fold(vv_Ctx *c, const char *key, const char *title, vv_Msg on_toggle) {
  const vv_Theme *t = vv_theme();

  uint32_t head = vv_box_keyed(c, key, strlen(key),
      VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .padding = {8,6,8,6}, .gap = 6),
      VV_STYLE(.bg = t->surface, .radius = {6,6,6,6}));

  bool *open = vv_state(c, head, bool);          // persistent per-node flag
  if (vv_clicked(c, head)) {
    *open = !*open;
    if (on_toggle) vv_emit(c, on_toggle, vv_pi(*open));
  }

  // Chevron rotates by declaring a different transform target; the spring does
  // the motion. Text as a stand-in glyph here.
  vv_text(c, *open ? "v" : ">", VV_STYLE(.fg = t->text_muted));
  vv_text(c, title, VV_STYLE(.fg = t->text));
  vv_end_box(c);
  return *open;
}
```

Called like any built-in:

```c
if (my_fold(c, "adv", "Advanced", MSG_FOLD)) {
  /* build the section's contents */
}
```

Notes:

- Take a primary `vv_Msg` (and payload) for the main action; add a `vv_On`
  parameter if you want to expose hover/press hooks — mirror the built-ins'
  `_on` convention.
- Emit from the widget with `vv_emit(ctx, msg, payload)`; never mutate app state
  from inside a widget (that's `update`'s job).
- Give containers a stable `key` when they live in loops so identity — and thus
  animation and `vv_state` — survives reordering.

### Raw-GPU content: the custom-draw hatch

When a node's pixels come from your own GPU code — a 3D scene, a plot, a shader
viewport — reach for `vv_custom` instead of Verve's primitives. It places a leaf
that lays out, hit-tests, and spring-animates its rect like any box, but whose
interior is filled by a callback the backend invokes with the on-screen rect:

```c
static void draw_canvas(void *ud, vv_Rect r) { /* raw GL into r */ }

App *app = ...;
app->cb = (vv_CustomDraw){ .fn = draw_canvas, .ud = app }; // must outlive the frame

// in view():
vv_custom(c, "canvas", 6, &app->cb,
          VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1)));
```

The backend scissors to the node's rect and restores GL state around the call,
so your drawing can't corrupt the UI and the UI can't clip into your viewport.
`draw` must point at app-persistent memory (not the frame arena). See
`examples/playground.c` for a live shader viewport wrapped in Verve controls.

---

## 9. Performance: idle mode and rebuild gating

Verve runs three frame tiers: **BUILD** (reconcile the tree), **PRESENT**
(advance springs, re-emit commands), and **IDLE** (nothing changed — skip the
frame). `vv_run_frame` picks the tier for you:

- A rebuild happens only when hover/focus changed, a press/release edge fired,
  something is being dragged, there was wheel/key/text input, the tree was
  marked dirty, or it's the first frame. **Plain mouse motion that doesn't
  change what's hovered does not rebuild.**
- With `vv_set_idle_mode(ctx, true)`, once springs settle and there's no input,
  `vv_run_frame` returns `NULL` and you skip drawing/swapping — effectively 0%
  CPU on a static screen.
- Force a rebuild yourself with `vv_invalidate(ctx)` (e.g. after loading data,
  or on a hot-reload swap).

If you need a widget to react to raw cursor movement (a custom canvas, a
hover-tracking tooltip), give it a `.move` message via `vv_on` — that's the
explicit opt-in back into per-motion rebuilds, scoped to just that node.

---

## 10. Hot-reloading the view (optional workflow)

`examples/hot/` shows editing your UI while the app keeps running. The trick:
**state lives in the host; only the `view`/`update` functions live in a `.so`.**
The host `dlopen`s the module and swaps it when the file's mtime changes, so a
reload preserves the counter, scroll position, everything.

- `examples/hot/app.h` — shared `App` state + message enum, plus the two
  exported symbols (`view_build`, `view_update`).
- `examples/hot/view.c` — the reloadable module. Uses only the public API.
- `examples/hot/host.c` — owns the window/GL/state/loop, polls the mtime, and
  `dlopen`s a fresh copy on change (copied to a unique `/tmp` name each time to
  defeat `dlopen`'s path cache). On reload it calls `vv_invalidate` so the new
  output shows even under idle mode.

```sh
make hot          # build just build/hotview.so
./build/hotdemo   # run it
# ...now edit examples/hot/view.c, then `make hot` again — the window updates
```

This is the cheap path to a live-editing feel without embedding a scripting
language.

---

## 11. Build & wiring reference

```sh
make            # library + tests + headless demo (clang)
make test       # run the suite
make gui        # SDL3/GL windowed examples (mycounter, sevenguis, gui_demo)
make hot        # the hot-reload .so
```

The **core** (`libverve.a`) has no backend dependency — it emits a flat
`vv_CommandBuffer`. Rendering is a separate concern: the bundled backend
(`backends/vv_sdl_gl.c`, SDL3 + OpenGL 3.3 + libepoxy + stb_truetype) turns that
buffer into pixels. To port to another backend you implement the `vv_Backend`
interface and a `vv_render` dispatch; the application code above doesn't change.

Minimal wiring for any backend:

1. `vv_init(&ctx)` once; `vv_shutdown` at the end.
2. `vv_set_measure_fn(&ctx, measure, ud)` so layout can size text.
3. Each frame: feed input + window size, call `vv_run_frame`, and if it returns
   non-NULL hand the buffer to `vv_render`.

---

## 12. Debugging: the drop-in inspector

`examples/inspect/vv_inspect.h` is a single-header, DevTools-style inspector you
attach to any app. It reads your retained pool read-only and overlays a tree +
properties panel on the right edge — including the one thing a DOM inspector
can't show: **`layout_rect` (target) vs `actual_rect` (where the spring is right
now)**, plus per-node spring velocity and settled/springing state. Hover or click
the app to inspect a node; click a tree row to pin it.

Attaching is `#include` + three lines in your loop — nothing in your
`view`/`update` changes:

```c
#define VV_INSPECT_IMPL     // in exactly one .c file
#include "vv_inspect.h"
...
vv_Inspector ins;
vv_inspect_init(&ins, &ctx, my_measure, my_ud);   // once

// per frame, wrapping your existing app frame:
vv_Input app_in = vv_inspect_split(&ins, in, w, h);      // routes the pointer
vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &app_in, update, view, &state);
vv_CommandBuffer *ov   = vv_inspect_render(&ins, dt, w, h, dpi);
if (cmds) vv_render(be, cmds, w, h, dpi);   // app, then...
if (ov)   vv_render(be, ov,  w, h, dpi);    // ...inspector on top
```

Toggle with `vv_inspect_toggle(&ins)` (bind it to any key your backend exposes).
It's compile-in and same-process — an overlay, not attach-to-a-running-process —
and read-only for now. See `examples/inspector.c` for the full wiring.

---

## Desktop features: windows, menus, dialogs, multi-line text

Four things a real desktop app needs, all demonstrated in `examples/showcase.c`
and used together in `examples/theme_editor.c`.

**Multi-line editing.** `vv_text_area(c, key, buf, cap, height, placeholder, msg)`
is `vv_text_field`'s big sibling: Enter inserts a newline, Up/Down move between
lines (keeping a goal column), Home/End act per line, selection spans rows, and
content scrolls. Pass `height <= 0` to grow and fill the parent pane.

**Menus, popovers, tooltips.** These are in-app overlays (portable, spring-
animated) — not OS menu bars, which SDL3 doesn't expose cross-platform. They set
a layout `.z`, so the painter (and hit testing) lift them above the normal tree.
That means you **declare them inline**, right where they're logically relevant —
no "build overlays last" bookkeeping:

```c
vv_menubar_begin(c);
uint32_t file = vv_menu_title(c, "file", "File");
if (vv_menu_is_open(c, file)) {                 // dropdown declared with its title
    vv_Rect r = vv_node(c, file)->actual_rect;
    vv_menu_begin(c, "filemenu", vv_v2(r.x, r.y + r.h));
    if (vv_menu_item(c, "open", "Open...", "Ctrl+O")) vv_emit(c, MSG_OPEN, VV_NO_PAYLOAD);
    vv_menu_end(c);
}
vv_menubar_end(c);
```

Menus self-manage which one is open and dismiss on outside-click/Escape (a full-
window scrim). Any node with `.z > 0` becomes an overlay, so this is a general
z-index — not menu-only. `vv_tooltip(c, id, text)` shows a hover-delayed label.

For popovers there are two flavors: `vv_popover_begin(c, key, at, width,
close_msg)` for app-owned state (the scrim emits `close_msg`), and
`vv_popover_open(c, key, at, width, &open)` which flips a `bool` directly — pair
it with `vv_ui_state` (below) for a popover with *no* app plumbing at all:

```c
bool *open = vv_ui_state(c, "insert-popover", bool);   // view-local, no App field
if (vv_clicked(c, aa_button)) *open = !*open;
if (*open) {
    vv_popover_open(c, "pop", at, 240, open);          // scrim/Escape clear *open
    vv_button(c, "date", "Date stamp", MSG_SNIPPET, vv_ps("[2026-07-19] "));
    vv_popover_end(c);
}
```

**Transient UI state (`vv_ui_state`).** The pure-`view` rule means state lives
outside `view` — but a lot of state is *view-local*: "is this popover open", a
hovered index, a draft string. Routing each through an App field + message +
`update` case is ceremony. `vv_ui_state(ctx, "key", T)` gives a `T*` that is
zeroed on first use and persists across frames (session-lived, keyed by string,
independent of any node's lifetime). Use it for local flags; keep the message/
`update` path for state with real consequences (saves, navigation, undo). The
self-managing menu open-state is the same idea, hidden inside the widget.

**Native file dialogs.** The real OS picker, via SDL3, is asynchronous — the
callback fires later during the pump:

```c
case MSG_OPEN: vv_app_open_file(app, "Text", "txt;md", on_open, &state); break;
// void on_open(void *ud, const char *path) — path is NULL if cancelled.
```

**Multiple windows.** The app owns windows; each has its own `vv_Ctx` (design
open-question 2). Open a second window sharing the GL context with
`vv_app_open_child(parent, title, w, h)`, then drive every window from one pump:

```c
while (vv_app_pump_all() > 0) {         // routes events to each window by ID
    if (vv_app_should_close(main)) break;
    // for each window: vv_run_frame(&its_ctx, dt, vv_app_input(its_app), ...)
    //                  then frame_begin/vv_render/frame_end on that window
}
```

**Cross-window invalidation (gotcha).** Each window has its *own* `vv_Ctx` with
its own rebuild gating, so a change made in one window does not, by itself, make
another window rebuild — a second window mirroring shared state will look stale
until its own input (e.g. a hover) forces a build. When shared state changes,
invalidate the other windows explicitly. The cheap pattern is a version counter:
bump it on every edit, and invalidate any window whose last-seen version differs.

```c
if (win.seen_version != state.version) { vv_invalidate(&win.ctx); win.seen_version = state.version; }
```

`examples/showcase.c`'s live preview does exactly this. Idle-mode single-window
apps should also sleep when `vv_run_frame` returns NULL — call
`vv_app_wait_event(app, 16)` instead of busy-spinning, or the busy loop can
starve the compositor's frame cadence and make animations look like they only
advance when you move the mouse.

**Resizable panels (`vv_splitter`).** Multi-panel layouts are just nested
`VV_ROW`/`VV_COLUMN` with `vv_fixed`/`vv_grow`. To make a divide draggable, keep
the pane's size in app state and drop a `vv_splitter` between the panes; it emits
a resize message you clamp and store, and the panes FLIP-spring to the new size.
Set `trailing = true` for a right/bottom-docked pane so a drag toward it shrinks
it (no sign juggling). See `examples/panels.c`.

```c
left .w = vv_fixed(a->left_w);
vv_splitter(c, "sl", VV_ROW, /*trailing*/ false, a->left_w, 140, 460, MSG_LEFT);
center .w = vv_grow(1);
vv_splitter(c, "sr", VV_ROW, /*trailing*/ true,  a->right_w, 160, 520, MSG_RIGHT);
right .w = vv_fixed(a->right_w);
```

---

## Where to look next

| You want… | Read |
|---|---|
| The full architecture & rationale | `verve-design.md` |
| A minimal end-to-end app | `examples/mycounter.c` |
| Two-way value bindings (no update case) | `examples/bindings.c` |
| A bigger app (7GUIs tasks) | `examples/sevenguis.c` |
| Drag & drop with FLIP-spring reorder | `examples/kanban.c` |
| Perceptual color (OKLab, contrast, ramps) | `examples/palette.c` |
| Per-node color springs over a big grid | `examples/habit.c` |
| State-driven transitions (tabs, async, toasts) | `examples/transitions.c` |
| Logic/view split + virtualized 10k-row table | `examples/table.c` |
| A pure scorer driving an animated fuzzy finder | `examples/finder.c` |
| A drop-in tree/style inspector | `examples/inspect/vv_inspect.h` |
| Raw-GL viewport inside the UI (custom draw) | `examples/playground.c` |
| Menus, popovers, tooltips, multi-window, native dialogs | `examples/showcase.c` |
| A real app: live theme editor (all of the above) | `examples/theme_editor.c` |
| Resizable multi-panel (IDE shell) with splitters | `examples/panels.c` |
| A complex stateful widget (calendar) that stays one message | `examples/dates.c` |
| Radio/progress/stepper/tabs/combobox/tree + context menu | `examples/gallery.c` |
| Live-editing workflow | `examples/hot/` |
| Exact signatures | headers in `include/verve/` |

Run any of the windowed demos with `make gui` then `./build/<name>`.
