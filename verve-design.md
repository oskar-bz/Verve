# Verve — Design Document

*An animation-native, scriptable UI library in C for graphics-heavy applications.*

`verve` is a placeholder name; C prefix `vv_`. Swap freely.

---

## 1. Goals and non-goals

### Goals

1. **Animation-native.** Transitions are the default, not an add-on. A user should never write a `lerp` call to get a smooth hover, a smooth layout reflow, or a smooth enter/exit.
2. **Immediate-mode ergonomics.** Building UI is a function call per frame. No widget lifetimes, no callback plumbing, no observer registration.
3. **Backend-agnostic.** The core emits a flat render command buffer and nothing else. It never touches a GL call.
4. **Scriptable from LuaJIT**, with hot reload as a first-class workflow, not a hack.
5. **Suited to visualizers**, not CRUD. Dense parameter panels, viewports, overlays, real-time data — not forms and tables.
6. **Small enough to hold in one head.** Target ~8–12k lines for the core.

### Non-goals

- Bidirectional text. Explicitly out of scope.
- IME / composition input. Out of scope (see §9.4 — this constrains backend choice).
- Complex text shaping (HarfBuzz, ligature-heavy scripts). Latin + basic diacritics only.
- Accessibility trees (AT-SPI / UIA). Acknowledged debt, not v1.
- Native platform widgets. Everything is drawn.
- HTML/CSS fidelity. We borrow flexbox *vocabulary*, not its semantics.

### The distinguishing bet

Every UI library either has immediate-mode ergonomics *or* animation quality. Verve's bet is that a reconciled retained tree behind an immediate-mode API gets both, and that this matters most for graphics-heavy tools where motion carries meaning.

---

## 2. Architecture overview

### 2.0 What paradigm this is

"Immediate mode" conflates three independent axes. Being precise about them prevents a recurring confusion:

| Axis | Dear ImGui | **Verve** | Qt |
|---|---|---|---|
| **API shape** — declare vs. construct/mutate | declare | **declare** | mutate |
| **State ownership** — who holds scroll, hover, animation | app | **library** | library |
| **Update cadence** — every frame vs. on change | every frame | **every frame (idle mode opt-in, §4.2)** | on change |

Verve is immediate in its API, retained in its state, and every-frame in its cadence by default. Only the third axis is genuinely negotiable, and §4.2 covers when and why to change it.

The useful comparison: **this is React's architecture written in C.** React's API is also "re-run the render function"; React also does not re-run it every frame. The difference between Verve's default and React is scheduling policy, not paradigm.

### 2.1 Pipeline

```
  Lua / C user code
        │  immediate-mode calls, declaring TARGET style
        ▼
  ┌──────────────────────────────────────────────┐
  │ Reconciler      ID → persistent node          │
  ├──────────────────────────────────────────────┤
  │ Layout          4-pass, constraints down      │
  ├──────────────────────────────────────────────┤
  │ Animation       springs → ACTUAL style/rect   │
  ├──────────────────────────────────────────────┤
  │ Input           hit-test against actual rects │
  ├──────────────────────────────────────────────┤
  │ Command emitter flat, sorted, backend-free    │
  └──────────────────────────────────────────────┘
        │  vv_CommandBuffer
        ▼
  Backend (raylib+SDF v1, SDL3 later)
```

The central invariant, stated once because everything follows from it:

> **User code declares the target style for the current logical state. The retained node holds the actual value and its velocity. Springs close the gap.**

`.bg = hovered ? theme.accent : theme.surface` produces a smooth transition, because the node persists across frames and can remember where it was.

---

## 3. Identity and reconciliation

### 3.1 ID derivation

```c
typedef uint64_t vv_ID;

vv_ID vv_id(vv_ID parent, uint32_t seq, const char *key, size_t klen);
```

An ID is `hash(parent_id, child_sequence_index, optional_user_key)`. FNV-1a or wyhash; the space is large enough that collisions are ignorable in practice, but assert on collision in debug builds.

Deliberately **not** call-site based. Call-site hashing (`__FILE__`/`__LINE__`, `__COUNTER__`) works in C but has no Lua equivalent short of `debug.getinfo`, which costs roughly a microsecond per widget. Parent-scoped sequence indexing is language-agnostic, free, and behaves identically from both sides.

**The failure mode**, which must be documented prominently: conditional siblings shift sequence indices and break identity.

```lua
if show_header then ui.text("Header") end
ui.button("OK")            -- index changes when show_header flips
```

The fix is an explicit key, and the library should make this cheap and obvious:

```lua
ui.button("OK", { key = "ok" })
```

Rule of thumb for users: **any widget that can appear conditionally, or that lives in a loop over a reorderable list, needs an explicit key.** Everything else can rely on sequence.

A debug mode should detect suspicious identity churn — a node that dies and a structurally similar node that is born in the same frame at an adjacent index — and warn. This will save hours.

### 3.2 Node storage

```c
typedef struct vv_Node {
    vv_ID     id;
    uint32_t  parent, first_child, last_child, next_sibling;  // pool indices
    uint32_t  child_count;
    uint64_t  last_touched_frame;

    vv_LayoutDecl  decl;        // this frame's declared layout intent
    vv_Style       target;      // this frame's declared style
    vv_StyleAnim   actual;      // interpolated values + velocities

    vv_Rect   layout_rect;      // where layout says it goes
    vv_Rect   actual_rect;      // where it is drawn (springs toward layout_rect)
    vv_Rect   prev_layout_rect; // for FLIP

    vv_Spring enter, exit;      // 0→1 and 0→1
    uint32_t  flags;            // HOVERED | ACTIVE | FOCUSED | EXITING | CLIP | SCROLL

    void     *widget_state;     // scroll offset, text edit state, etc.
    uint32_t  widget_state_size;
} vv_Node;
```

Nodes live in a persistent pool with a freelist. A separate open-addressing map `vv_ID → pool index` provides lookup. Both survive across frames.

### 3.3 Lifecycle

- **Birth**: ID not in map → allocate, initialize `actual` from `target` (no animation on first appearance of style), start `enter` spring at 0.
- **Persistence**: ID found → update `target` and `decl`, stamp `last_touched_frame`.
- **Death**: not touched this frame → set `EXITING`, remove from layout participation, drive `exit` spring. When `exit` settles, free the node and its `widget_state`.

An exiting node is **not hit-testable** and does **not** participate in layout. It renders on top of its former position using its last `actual_rect`. This is what makes "fade out and shrink" possible at all — but note the consequence: siblings reflow immediately while the corpse fades. If you want the corpse to also collapse its space, that requires keeping it in layout with an animated-to-zero size; offer this as an opt-in flag (`VV_EXIT_COLLAPSE`) rather than the default, because it's more expensive and often not what you want.

---

## 4. Frame lifecycle

```c
vv_begin_frame(ctx, dt, &input);
  /* user code builds the tree */
vv_CommandBuffer *cmds = vv_end_frame(ctx);
backend_render(cmds);
```

### 4.1 Two phases, not one

The per-frame work splits into two loops of very different cost. Keeping them separable is the single most consequential structural decision in the pipeline, and it must be true from Phase 0 even though the scheduling that exploits it lands much later.

**Build** — expensive, scales with node count, runs user code:

1. **Reconcile** — mark untouched nodes as exiting.
2. **Layout pass 1** — bottom-up intrinsic widths (`FIT`).
3. **Layout pass 2** — top-down width distribution (`GROW`, `PERCENT`, `FIXED`).
4. **Layout pass 3** — bottom-up heights. Text wraps here, now that widths are final.
5. **Layout pass 4** — top-down height distribution and final positioning.

**Present** — cheap, no user code, no reconciliation, no layout:

6. **Animate** — tick every spring toward its target; compute `actual_rect` and `actual` style.
7. **Hit test** — against `actual_rect`, in reverse paint order.
8. **Emit** — walk the tree, produce commands with scissor/transform stack.

**Animation requires only Present.** Springs live on the retained tree and advance without user code running. A hover transition, a FLIP reflow, a spring settling — none of these need a rebuild. This is worth stating explicitly because the intuition runs the other way: "everything animates" sounds like it implies "everything rebuilds," and it does not.

### 4.2 Three-tier frame model

| Tier | Condition | Work |
|---|---|---|
| **Idle** | nothing dirty, all springs settled, no input | resubmit last command buffer, or skip entirely |
| **Present** | springs unsettled, tree unchanged | steps 6–8 only |
| **Build** | tree dirty | full pipeline, steps 1–8 |

Rough costs at a few hundred nodes: Build is 50–200 µs, Present is single-digit µs, Idle is free.

**Default: Build every frame.** Tier 2 (Present-only for animation) is implemented from the start, because it is nearly free once the phases are separated and it is what makes animation cheap. Tier 1 (idle) is deferred behind an explicit opt-in:

```c
vv_set_idle_mode(ctx, true);      // default: false
```

The reason for defaulting off is **debuggability, not performance**. Build-every-frame is unconditionally correct. The moment correctness depends on invalidation being right, every bug becomes "is this a logic error or a missed invalidation?" — a genuinely miserable question, because the symptom is a UI that silently fails to update and the cause is arbitrarily far away. Keeping the always-correct path as the default means you can always flip idle mode off to bisect.

For the primary use case this optimization buys little: a parametrized visualizer renders continuously at 60 fps anyway, the visualization dominates, and saving 150 µs of UI layout is noise. Where it genuinely pays is **idle power** — a tool left open for eight hours drawing an unchanging panel at 60 fps is a measurably worse citizen than one drawing nothing. That is the real argument, and it is a good one, but it is not urgent.

### 4.3 Invalidation (for idle mode)

Only relevant once idle mode is enabled. Four strategies, evaluated:

| Strategy | Precision | Cost | Failure mode |
|---|---|---|---|
| Manual `vv_invalidate()` | manual | free | stale UI, silent |
| Any input event → dirty | coarse | rebuilds on mouse move | none (safe) |
| **Write-barrier on state** | good | metatable dispatch | nested mutation missed |
| Fine-grained signals | exact | high complexity in C | none |

**Lua uses a write barrier.** `ui.store` returns a proxy whose `__newindex` marks the tree dirty:

```lua
local s = ui.store("counter", { count = 0 })
s.count = s.count + 1     -- automatically invalidates. No handler, no return value.
```

This preserves the inline immediate-mode call site — `if ui.button("Count") then s.count = s.count + 1 end` — while getting event-driven rebuild for free. Known gap: nested table mutation (`s.people[3].name = "x"`) writes through an inner table with no barrier. Either deep-proxy at some cost, or provide `ui.touch(s.people)` for the nested case. Prefer the latter; deep proxying every stored table is a lot of metatable overhead for a case users can be taught.

**C has no metatables**, so it gets either manual `vv_invalidate(ctx)` or a macro that assigns and dirties in one expression:

```c
#define VV_SET(ctx, lvalue, val)  ((lvalue) = (val), vv_invalidate(ctx))
```

Explicitly rejected: **handler functions returning a "needs redraw" boolean.** It requires the handler to know what the view depends on, which is the exact coupling immediate mode exists to eliminate, and getting it wrong fails silently. It also reintroduces message-dispatch indirection — the button's behaviour ends up three screens from the button — while getting none of Elm's compensating benefit, since neither C nor Lua gives exhaustive compile-time checking of state transitions.

### 4.4 Interaction styling must be declarative

There is a prerequisite for idle mode that is easy to miss and fatal if missed.

If interaction styling lives in user code:

```lua
{ bg = hovered and theme.accent or theme.surface }     -- evaluated during Build
```

…then every hover change requires a Build. The mouse moves constantly, so you rebuild on nearly every frame regardless, and idle mode buys nothing.

The fix is declarative state variants, which the library can retarget without running user code:

```lua
ui.box({...}, {
  bg     = theme.surface,
  hover  = { bg = theme.accent },
  active = { bg = theme.accent_pressed, transform = { scale = 0.98 } },
  focus  = { border_color = theme.focus },
})
```

This is exactly why CSS `:hover` does not trigger a JavaScript re-render — same problem, same solution.

**Both forms are supported.** The library detects which was used: a node styled purely via variants stays Present-tier, and a node whose build code calls `vv_hovered()` gets flagged `VV_REBUILD_ON_INTERACTION`. You pay for the flexibility of an arbitrary ternary only on the nodes where you actually use it.

Add the variants regardless of scheduling — they are better ergonomics on their own, and they make the common case declarative enough that the library can reason about it.

### 4.5 The one-frame interaction lag

Hit testing happens *after* the user's build code has already run, so `vv_button()` returns a click detected against **last frame's geometry**. This is the standard immediate-mode compromise and both ImGui and Clay accept it.

It is almost always invisible at 60 Hz. It becomes visible when geometry moves fast — which, in an animation-native library, is more often than usual. Two mitigations, in order of preference:

- Hit-test against `actual_rect` (the animated one), not `layout_rect`. Users click what they *see*, not what layout intends. This is non-negotiable and fixes the common case.
- For the rare pathological case (a button flying across the screen), offer `VV_HITTEST_LAYOUT` per node to test against the destination instead.

Document the lag rather than hiding it.

---

## 5. Layout

**Model: flexbox vocabulary on box-constraint mechanics.** Users think in rows, columns, gaps, and grow factors. The engine runs Flutter-style constraints-down/sizes-up, which is O(n), stable frame-to-frame, and cheap enough to run unconditionally — a hard requirement for FLIP.

### 5.1 Sizing modes

Four modes, per axis. Keep it to four.

| Mode | Meaning |
|---|---|
| `FIXED(n)` | Exactly n logical pixels |
| `FIT` | Intrinsic size of content, clamped to `[min, max]` |
| `GROW(w)` | Share of leftover space in the parent's main axis, weight `w` |
| `PERCENT(p)` | Fraction of the parent's resolved size on this axis |

```c
typedef struct {
    vv_SizeMode mode;
    float value;         // n, weight, or fraction
    float min, max;      // clamps, applied in all modes
} vv_Size;
```

### 5.2 Container declaration

```c
typedef struct vv_LayoutDecl {
    vv_Axis   dir;              // ROW | COLUMN
    vv_Size   w, h;
    vv_Edges  padding;          // l, t, r, b
    float     gap;
    vv_Align  main, cross;      // START | CENTER | END | SPACE_BETWEEN (main only)
    bool      wrap;
    bool      clip;
    bool      scroll_x, scroll_y;
    bool      focusable;        // participates in tab traversal
    bool      disabled;         // suppresses hit test + focus, selects the
                                // `disabled` style variant. Inherited by
                                // descendants. NOT a style — it changes
                                // behaviour, so it belongs on the node.
    vv_Rect   absolute;         // if set, escapes flow (tooltips, popovers, drags)
    int       z;                // layer for popovers/overlays
} vv_LayoutDecl;
```

### 5.3 Why width fully before height

Text is `height-for-width`: you cannot know a paragraph's height until you know its width. Resolving the horizontal axis completely, then the vertical, makes wrapping fall out naturally instead of requiring iteration. This is the main reason to take box constraints over a naive flexbox implementation.

The cost is that `height-for-width` only works in that direction. A `ROW` whose height determines a child's width (rotated text, aspect-locked images) needs a special case. Provide `aspect_ratio` as an explicit escape hatch rather than making the general algorithm bidirectional.

### 5.4 Overflow

When children exceed available space:

1. Shrink `GROW` children down to their `min`.
2. Then shrink `FIT` children proportionally toward their `min`.
3. Then, if `clip` is set, clip. If `scroll` is set, scroll. Otherwise overflow visibly (and warn in debug).

Never silently produce negative sizes. Clamp at zero and flag it.

### 5.5 Virtualization

Large grids cannot be built naively. A 26 × 100 spreadsheet is 2,600 nodes per Build: four layout passes, 2,600 hash lookups, and a text measurement each — plausibly 2–4 ms, which is most of a frame budget spent on a static grid.

`vv_rows(ctx, count, row_height, fn)` consults the enclosing scroll viewport, computes the visible index range, emits a spacer for the region above, builds only the visible rows, and emits a spacer below. Roughly 25 × 26 = 650 nodes, which is comfortable.

**The cost is identity.** Culled nodes are untouched, so they exit and are freed; scrolling back in creates *new* nodes with no animation continuity and no retained widget state. For a spreadsheet this is entirely acceptable. For a list of animating cards it would be wrong.

Virtualization and animation continuity are in direct tension, and the library should surface that as an explicit user choice rather than deciding silently. Provide `vv_rows` as opt-in, document the tradeoff at the call site, and consider a `VV_KEEP_STATE` variant that retains widget state (but not animation) for culled rows at the cost of never freeing it.

One subtlety: exiting nodes normally animate out (§3.3). Culled rows must **not** — a scroll would leave a trail of fading corpses. Nodes removed by culling are freed immediately, bypassing the exit spring. The culler must mark them so reconciliation can tell "scrolled away" from "genuinely deleted."

### 5.6 Scrolling

A node with `scroll_y` gets persistent `scroll_offset` in `widget_state`. The offset is itself a spring — targets snap, actual glides. Clamping is a soft clamp: overscroll is allowed with a resistance curve, and the spring pulls back on release. This is roughly free given the spring infrastructure and is disproportionately responsible for a UI feeling good.

---

## 6. Animation

### 6.1 Springs, not tweens

```c
typedef struct {
    float x;          // current
    float v;          // velocity
    float target;
    float response;   // seconds; roughly the period
    float damping;    // 1.0 = critically damped, <1 bounces
    bool  settled;
} vv_Spring;
```

Duration+easing tweens cannot be interrupted gracefully: retargeting mid-flight either snaps velocity to zero or requires bookkeeping to fake continuity. A spring carries velocity inherently, so retargeting is *the normal case*. Since a UI's targets change constantly (hover in, hover out before settling, hover in again), this is the correct primitive.

Parametrize as `response` / `damping` rather than `stiffness` / `mass` — humans can tune the former by feel. Defaults: `response = 0.25`, `damping = 1.0`. Provide named presets: `VV_SNAPPY` (0.15, 1.0), `VV_SMOOTH` (0.35, 1.0), `VV_BOUNCY` (0.4, 0.6).

**Integration.** Semi-implicit Euler with substepping is fine to start:

```c
static void spring_step(vv_Spring *s, float dt) {
    const float omega = 6.2831853f / s->response;
    int   steps = (int)(dt / (1.0f/240.0f)) + 1;
    float h     = dt / steps;
    for (int i = 0; i < steps; i++) {
        float a = -omega*omega*(s->x - s->target) - 2.0f*s->damping*omega*s->v;
        s->v += a * h;
        s->x += s->v * h;
    }
    if (fabsf(s->x - s->target) < s->eps && fabsf(s->v) < s->eps*10.0f) {
        s->x = s->target; s->v = 0.0f; s->settled = true;
    }
}
```

Upgrade to the closed-form analytic solution once profiling justifies it — it is exact at any `dt` and removes the substepping loop. Worth doing eventually because it also makes frame-rate-independent behaviour provable rather than approximate.

### 6.2 Frame scheduling

Maintain a count of unsettled springs. If zero, and no input arrived, and no user-requested continuous redraw, skip the frame entirely (or render at 1 Hz for safety).

For visualizer applications the user will often force continuous redraw anyway, so provide `vv_request_animation_frame(ctx)` and don't over-engineer damage tracking in v1.

### 6.3 Color interpolation in Oklab

Interpolating sRGB gives muddy or grey midpoints — a blue-to-yellow transition passes through mud. Convert sRGB → linear → Oklab, interpolate, convert back.

This single choice does an outsized share of the work in making motion look professional. Interpolate `L`, `a`, `b` linearly; for large hue rotations consider Oklch with shortest-arc hue interpolation, but Oklab is sufficient and simpler.

Cache nothing; at a few thousand nodes the conversion cost is noise.

### 6.4 What animates

| Property | Mechanism |
|---|---|
| Colors (bg, border, text, shadow) | Oklab spring |
| Scalars (radius, opacity, border width, padding) | Spring |
| Position and size | FLIP (§6.5) |
| Transform (scale, rotation) | Spring per component |
| Enter / exit | Dedicated 0→1 springs |
| Everything else | Snaps |

Opt out per property with `.transition = VV_INSTANT`, or per node with a spring override in the style struct.

### 6.4.1 Springs are for discrete changes only

**Continuously-driven values must opt out of animation.** A progress bar whose width is `elapsed / duration` already changes smoothly; springing it on top adds lag and makes the display trail reality.

This needs to be stated prominently in user documentation, because the intuition runs the other way — "animation-native" suggests more animation is always better — and a user who hits it will conclude the library is laggy rather than that they misused it. The rule:

> If the value changes every frame anyway, mark it `VV_INSTANT`. Springs exist to smooth *jumps*.

Where a continuously-driven value also takes occasional jumps (a timer whose duration is adjusted by a slider), spring the *input* rather than the output.

### 6.5 FLIP for layout transitions

Each node keeps `prev_layout_rect`. When layout produces a different `layout_rect`, `actual_rect` does not snap — it springs from where it was toward where it now belongs. Four springs (x, y, w, h), or two if you decompose into position and scale.

This is only possible because the tree is retained, and it is the payoff for the entire architecture: **inserting an item into a list animates every other item into its new position, with no user code.**

Guard against pathology: if the delta exceeds some threshold (a full viewport, say), snap instead of animating. Otherwise a scroll jump or a panel swap produces an absurd fly-across.

### 6.6 Enter and exit

- **Enter**: spring 0→1, drives opacity and a slight scale (0.96→1.0) by default. Style is *not* animated on birth — a node appears with its declared colors.
- **Exit**: as described in §3.3. Default is opacity 1→0 plus scale to 0.98.

Both overridable per node. Users building visualizers will want custom ones.

---

## 7. Styling

### 7.1 Declaration structs

C99 designated initializers, zero-init meaning "inherit from theme":

```c
typedef struct vv_Style {
    vv_Color  bg;
    vv_Color  fg;              // text
    vv_Corners radius;         // per-corner
    vv_Edges  border_width;    // per-side
    vv_Color  border_color;
    vv_Shadow shadow;          // blur, offset, color, inset
    float     opacity;
    vv_Mat23  transform;
    vv_FontID font;
    float     font_size;
    vv_SpringParams spring;    // per-node override
    uint32_t  transition_mask; // bits to exclude from animation

    /* Declarative state variants (§4.4). Sparse overrides applied on top
       of the base when the node is in that state. NULL = no override. */
    const struct vv_Style *hover, *active, *focus, *disabled;
} vv_Style;
```

Variants are sparse overlays, not complete styles: only fields explicitly set are applied. Since zero means "inherit" everywhere else in the system, variants need a per-field presence mask (or a parallel `set` bitfield) so that `.bg = {0,0,0,0}` can mean "transparent black" rather than "unset". This is the one place the zero-init-means-default convention breaks down and needs explicit machinery.

Resolution order becomes: `zero → theme role default → base style → disabled → focus → hover → active`, later winning. The result lands in `target`, and springs carry `actual` toward it as before — so variant transitions animate through the same path as everything else, with no special cases.

```c
vv_box((vv_LayoutDecl){ .dir = VV_ROW, .gap = 8, .padding = vv_all(12) },
       (vv_Style){ .bg = theme.surface, .radius = vv_r(8) });
```

Chosen over an ImGui-style push/pop stack because push/pop produces a resolved style with no identity — you cannot animate it. Struct-per-node maps directly onto a node, and maps directly onto a Lua table.

A theme is just a `vv_Style` used as the fallback source, plus a palette struct. Themes are values; swapping one is assignment, and because everything springs, **theme changes animate for free.** That is a nice demo.

### 7.2 Style resolution order

`zero → theme default for role → explicit per-node fields`. Resolved into `target`, which springs feed into `actual`, which is what gets emitted.

No cascade, no selectors, no inheritance chain. Text color inherits from parent explicitly (one special case, because it's genuinely useful); nothing else does. Resist CSS.

---

## 8. Render command protocol

The core's entire output.

```c
typedef enum {
    VV_CMD_RECT,
    VV_CMD_TEXT,
    VV_CMD_IMAGE,
    VV_CMD_SCISSOR_PUSH, VV_CMD_SCISSOR_POP,
    VV_CMD_TRANSFORM_PUSH, VV_CMD_TRANSFORM_POP,
    VV_CMD_CUSTOM,
} vv_CmdKind;

typedef struct {
    vv_Rect    rect;
    vv_Corners radius;
    vv_Color   fill_a, fill_b;    // equal = solid; else gradient
    float      gradient_angle;
    vv_Edges   border_width;
    vv_Color   border_color;
    vv_Shadow  shadow;
} vv_CmdRect;

typedef struct {
    const char *utf8;      // points into the frame arena
    uint32_t    len;
    vv_FontID   font;
    float       size;
    vv_Color    color;
    vv_Vec2     origin;    // baseline, left
} vv_CmdText;

typedef struct {
    vv_CmdKind kind;
    union { vv_CmdRect rect; vv_CmdText text; vv_CmdImage image;
            vv_Rect scissor; vv_Mat23 xform; vv_CmdCustom custom; } as;
} vv_Command;
```

A flat array, in paint order. The backend iterates and translates. It knows nothing about buttons, hover, or themes.

`VV_CMD_CUSTOM` carries an opaque `uint32_t id` plus a `void*` payload and a rect; the backend dispatches to user code. This is the viewport escape hatch — your actual visualization renders here, correctly clipped and positioned by the UI's layout, with no coupling in either direction.

### 8.1 Text runs, not glyphs

`VV_CMD_TEXT` deliberately carries a string, not positioned glyphs. Rationale: it keeps the command buffer small, keeps the font atlas entirely in the backend, and means the core never owns a texture. The cost is that the core must still *measure* text during layout — so the backend exposes a measurement callback used at layout time (§15). This is the one place the core/backend separation is not clean, and it is unavoidable.

---

## 9. Rendering backend (raylib + SDF, v1)

### 9.1 The single-shader trick

Fill, per-corner radius, per-side border, drop shadow, and antialiasing all collapse into one fragment shader over a quad. The classic rounded-box SDF:

```glsl
float sd_round_box(vec2 p, vec2 half_size, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    vec2 q = abs(p) - half_size + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}
```

- **Fill**: `smoothstep(aa, -aa, d)` where `aa = fwidth(d)`.
- **Border**: ring via `abs(d + w*0.5) - w*0.5`.
- **Shadow**: expand the quad by `blur + spread`, evaluate the SDF offset by the shadow offset, alpha from `smoothstep(blur, -blur, d)`. A true Gaussian is nicer but this approximation is visually fine and one instruction.

With per-instance vertex attributes this is potentially **one draw call for the entire UI chrome**. The command set in §8 was designed backwards from this shader; that's why `VV_CMD_RECT` bundles fill, border, and shadow rather than splitting them.

### 9.2 raylib specifics

raylib supports custom shaders fine. Instancing via `rlgl` is available but fiddly; the simpler v1 path is a dynamic vertex buffer with per-rect attributes duplicated across four vertices. Wasteful, irrelevant at UI scale.

Watch for: raylib's own batcher will flush when you change shaders or textures, so batch all rects, then all text, then all images — the command emitter should already produce runs, but the backend may reorder within a scissor region.

### 9.3 Overdraw

Shadows are the enemy. A panel with a large blur radius expands its quad substantially, and nested panels stack. Mitigations: cap blur radius, skip shadow rendering when `shadow.color.a == 0` (the common case, so check first), and consider a debug overdraw visualization early — it will be informative.

### 9.4 The IME constraint, and why it's now moot

raylib exposes `GetCharPressed` but no composition state — no preedit string, no candidate window positioning. Real IME is **not implementable on raylib without patching it.**

Since IME is out of scope, this ceases to be a forcing function, and raylib is viable indefinitely. Keep the backend interface clean anyway (§15) — the reasons to move to SDL3 later are better multi-window support, better DPI handling, and the SDL3 GPU API — but there is no longer a deadline.

---

## 10. Text

Scope is Latin plus diacritics: no shaping, no bidi, no IME.

### 10.1 Rasterization

**Use `stb_truetype` into a dynamic atlas, keyed by `(font, px_size, codepoint)`, with LRU eviction.** Sharp at UI sizes, simple, and correct.

MSDF is tempting for an animation-native library — crisp at any scale, ideal for a zoomable visualizer. But MSDF is genuinely worse at small sizes: no hinting, thin stems break down, and 13px body text looks soft. The pragmatic answer is a **hybrid**: bitmap atlas for UI chrome at discrete sizes, MSDF as an opt-in for large or continuously-scaled text (viewport labels, titles). Defer MSDF to a later phase; the atlas abstraction should not care which backs a given font.

Re-rasterize on DPI change. Cache by device pixel size, not logical size.

### 10.2 Layout

- Measure: sum advances, apply kerning from the `kern` table (or `GPOS` pair positioning if you want it; `kern` is enough for most fonts).
- Wrap: break on whitespace; break anywhere within CJK ranges. UAX #14 is overkill — a whitespace + CJK rule covers the realistic cases here.
- Grapheme clusters: **still required even without bidi or shaping.** Combining accents and emoji ZWJ sequences must not be splittable by the cursor. Use `libgrapheme` — small, single purpose, and prevents a category of embarrassing bugs.
- Cache measurement results keyed by `(string hash, font, size, wrap width)`. Layout runs every frame; text measurement is the expensive part.

### 10.3 Editing

**Use `stb_textedit.h`.** Single header, handles cursor motion, selection, word boundaries, and the undo stack. It's what ImGui uses. This removes months of subtle bug-fixing.

Per-node state in `widget_state`:

```c
typedef struct {
    STB_TexteditState  state;
    char              *buf;
    int                len, cap;
    vv_Spring          cursor_x, cursor_y;   // animated caret
    vv_Spring          scroll;
} vv_TextEditState;
```

The animated caret is a small thing that reads as high quality — the cursor glides between positions rather than teleporting. Nearly free given the spring system.

Clipboard goes through the backend interface (raylib provides `GetClipboardText` / `SetClipboardText`).

Selection rendering: emit one `VV_CMD_RECT` per selected line fragment, beneath the text run.

---

## 11. Input and interaction

### 11.1 States

Three distinct concepts, often conflated:

- **Hovered** — pointer is over the node's `actual_rect` and nothing above it claims the pointer.
- **Active** — pressed, with pointer capture. Survives the pointer leaving the rect (essential for sliders and drags).
- **Focused** — keyboard target. Exactly one, or none.

```c
bool vv_hovered(vv_ID);
bool vv_pressed(vv_ID);     // went down this frame
bool vv_clicked(vv_ID);     // down and up both inside
bool vv_active(vv_ID);      // held with capture
vv_Vec2 vv_drag_delta(vv_ID);
```

### 11.2 Hit testing

Reverse paint order, respecting `z` layers and scissor rects, against `actual_rect`. First hit wins unless the node is marked pass-through.

Capture: on press, record the active ID. Until release, all pointer events route there regardless of position. Without this, dragging a slider fast breaks.

### 11.3 Keyboard

Events route to the focused node, then bubble to ancestors — so a panel can handle Escape without every child knowing. Since this is immediate mode, "bubbling" means: the focused node queries first, and consumed-event flags prevent ancestors from also handling it.

Tab traversal follows tree order. Nodes opt in with `VV_FOCUSABLE`.

Focus ring rendering should be automatic and, of course, animated — it glides between focused elements. This is both a quality signal and a real accessibility affordance, and it's essentially free.

---

## 12. Value bindings

Deliberately lightweight now, structured so that a full parameter system is an addition rather than a refactor.

```c
typedef enum { VV_VAL_F32, VV_VAL_I32, VV_VAL_BOOL, VV_VAL_COLOR, VV_VAL_STR } vv_ValueKind;

typedef struct {
    const char *name;
    const char *unit;
    float       min, max;
    float       curve;      // 1.0 linear; >1 skews toward min; log via flag
    uint32_t    flags;      // AUTOMATABLE | PRESET | READONLY | LOG
} vv_ValueMeta;

typedef struct {
    vv_ValueKind        kind;
    void               *ptr;     // points at user memory
    const vv_ValueMeta *meta;    // nullable in v1
} vv_Value;
```

Widgets take a `vv_Value` rather than a raw pointer:

```c
vv_slider(vv_id_key("cutoff"), vv_f32(&params.cutoff, &meta_cutoff));
```

In v1, only `min`/`max`/`curve` are read. `name`, `unit`, and `flags` exist and are ignored.

**The upgrade path**, deliberately left open: a registry of `vv_Value`s keyed by name gives you preset save/load, undo/redo (a value change *is* the atomic edit), auto-generated inspector panels (`vv_inspect(registry)`), and — the interesting one — automation. Your spring system already animates UI properties; pointing it at bound values extends "animation-native" from the chrome into the *data*. An LFO or keyframe curve driving a parameter is the difference between a visualizer and a *parametrized* visualizer. MIDI/OSC control then falls out nearly free.

None of that is v1. But because the metadata struct exists from day one, none of it requires touching widget signatures later.

### 12.1 Transactional editing

One pattern recurs often enough to belong in the core rather than in every user's code: **one undo entry per adjustment session, not per frame of dragging.** Dragging a slider for two seconds should produce a single undoable edit, not 120.

```c
vv_begin_edit(ctx, value);   // on drag start / popover open — snapshots
vv_end_edit(ctx, value);     // on release — commits iff the value differs
```

Every parameter-heavy application needs this, and hand-rolling it means each widget author reinvents the snapshot-and-compare dance. Fold it into the value registry when that lands (Phase 9); until then, built-in widgets should implement the pattern consistently so the eventual API matches established behaviour.

---

## 13. Memory model

Two allocators, both arenas.

**Persistent arena** — node pool with freelist, ID→index map, widget state, font atlases, text edit buffers. Grows; never freed during a session.

**Frame arena** — command buffer, layout scratch, text measurement temporaries, string copies. Reset wholesale at `vv_begin_frame`. This is what makes the immediate-mode API safe to call from Lua: transient Lua strings are copied into the frame arena, so the core never holds a reference into the GC heap past the frame boundary.

Rule: **no `malloc` outside the two arena implementations.** Everything is an offset or an index. This also makes the whole context trivially serializable for debugging — dump the node pool, diff two frames.

Node indices, not pointers, in the tree. Pool growth reallocates.

---

## 14. Widgets

### 14.1 The authoring model

**A widget is a function.** There is no widget type, no base class, no registration table, no vtable, and no lifecycle protocol. Built-in and user-defined widgets are indistinguishable, because the built-ins are written in exactly the API users have.

This is the acceptance test for the entire public API: **if `vv_slider` cannot be written without internal access, the API is wrong.** Any privileged call a built-in needs must either be exposed or designed away.

```c
bool vv_button(vv_Ctx *ctx, vv_ID id, const char *label) {
    vv_box(ctx, id,
        LAYOUT(.w = VV_FIT, .padding = vv_hv(12, 6), .focusable = true),
        STYLE(.bg = theme.button, .radius = vv_r(6),
              .hover  = &(vv_Style){ .bg = theme.button_hover },
              .active = &(vv_Style){ .bg = theme.button_active,
                                     .transform = vv_scale(0.98f) }))
    {
        vv_text(ctx, label, STYLE(.fg = theme.on_button));
    }
    return vv_clicked(ctx, id);
}
```

Note that this button never queries `vv_hovered()` — its interaction styling is entirely declarative, so it stays Present-tier under idle mode (§4.4). Built-ins should be written this way as a matter of course.

### 14.2 What widgets need beyond user-level API

Exactly four things.

**1. Persistent per-node state.** Scroll offset, text buffer, drag origin.

```c
typedef struct { float grab_offset; bool dragging; } SliderState;

SliderState *st = vv_state(ctx, id, SliderState);
```

Zeroed on first use, freed automatically when the node dies. This is the **only** genuinely privileged call in the widget API, and it is the mechanism that lets immediate-mode widgets be stateful without the application owning their internals. It writes to `vv_Node.widget_state` (§3.2); the size is captured from the type so the pool can free it correctly.

**2. Extra primitives inside their own rect.** A knob's indicator, a checkmark, a selection highlight — things layout cannot express. These emit directly into the current node's clip and transform context:

```c
vv_draw_rect(ctx, rect, STYLE(.bg = ..., .radius = ...));
vv_draw_text(ctx, "✓", origin, STYLE(...));
```

Most custom widgets need only this.

**3. The backend, for genuinely custom rendering.** Shaders, meshes, the visualization itself:

```c
vv_custom(ctx, id, MY_PLOT_KIND, &plot_data);   // → VV_CMD_CUSTOM
```

Two escape hatches at deliberately different levels: `vv_draw_*` for "more rectangles and text than layout gives me," `vv_custom` for "I need the GPU."

**4. An intrinsic size, if they are leaves.** Composite widgets derive it from children. A custom leaf either declares a fixed size or installs a measure callback:

```c
vv_set_measure(ctx, id, my_measure_fn, userdata);
```

Same mechanism text uses (§15), so there is no special case for text in the layout engine.

### 14.3 In Lua

```lua
local function toggle(value, opts)
  local n = ui.box({ w = 44, h = 24, key = opts.key, focusable = true },
                   { bg = value and theme.accent or theme.track, radius = 12 },
    function()
      ui.box({ w = 20, h = 20, absolute = { x = value and 22 or 2, y = 2 } },
             { bg = theme.knob, radius = 10 })
    end)

  if n.clicked then return not value, true end
  return value, false
end
```

The knob's `x` jumps between 2 and 22; the spring slides it. A working animated toggle in eight lines with no animation code.

Two divergences from C worth noting, both instances of §20.1:

- **Containers return an interaction record in Lua** (`n.clicked`, `n.hovered`), where C uses the ID plus query functions. The C block macro cannot return a value.
- **Lua widgets take and return values**; C widgets take pointers. Immediate mode means no reference is held past the frame, so Lua needs no pointer discipline at all.

**Hot reload covers widget definitions.** Widgets are ordinary Lua functions, so you can edit how your slider looks and watch it change live, mid-drag, without restarting. For a tool where you are tuning aesthetics by feel, this is a large part of the value proposition.

### 14.4 No registry

Resist adding one. Nothing in the architecture needs widget lookup by name. The one plausible candidate — auto-generating an inspector from bound values (§12) — needs a small `value_kind → function` table, which is not a plugin system and should not be allowed to become one.

### 14.5 Catalogue (v1)

Keep it small; the point is the framework.

**Primitives**: `box`, `text`, `image`, `spacer`, `custom` (viewport).

**Interactive**: `button`, `toggle`, `checkbox`, `radio`, `slider` (h/v), `drag_number` (drag-to-adjust, essential for parameter tweaking), `text_field`, `text_area`, `dropdown`, `color_picker`.

**Containers**: `panel`, `scroll_area`, `splitter` (draggable, animated), `tabs`, `collapsible`, `popover`, `tooltip`, `modal`.

**Visualizer-specific**: `plot_area` (axes, grid, pan/zoom, emits a `custom` for the content), `knob`, `xy_pad`, `curve_editor`.

That last group is where a library aimed at visualizers earns its keep, and it should be prototyped early enough to validate the architecture against real use.

---

## 15. Backend interface

```c
typedef struct vv_Backend {
    void *ctx;

    void (*begin)(void *ctx, int w, int h, float dpi_scale);
    void (*end)(void *ctx);

    void (*draw_rects)(void *ctx, const vv_CmdRect *rects, int n);
    void (*draw_text)(void *ctx, const vv_CmdText *runs, int n);
    void (*draw_image)(void *ctx, const vv_CmdImage *imgs, int n);

    void (*push_scissor)(void *ctx, vv_Rect r);
    void (*pop_scissor)(void *ctx);
    void (*push_transform)(void *ctx, vv_Mat23 m);
    void (*pop_transform)(void *ctx);

    void (*custom)(void *ctx, uint32_t id, void *payload, vv_Rect r);

    /* resources */
    vv_TexID  (*texture_create)(void *ctx, const void *px, int w, int h, vv_PixFmt);
    void      (*texture_destroy)(void *ctx, vv_TexID);
    vv_FontID (*font_load)(void *ctx, const void *ttf, size_t len);

    /* measurement — called during layout, not rendering */
    vv_Vec2 (*measure_text)(void *ctx, const char *s, int len,
                            vv_FontID, float size, float wrap_width);

    /* platform */
    const char *(*clipboard_get)(void *ctx);
    void        (*clipboard_set)(void *ctx, const char *s);
    void        (*set_cursor)(void *ctx, vv_CursorShape);
} vv_Backend;
```

Fifteen functions. A second backend should take a weekend; write the SDL3 one eventually purely to prove the abstraction holds, even if you never ship it.

`measure_text` is the leak in the abstraction — it's called from layout, not from rendering. Accept it and document why (§8.1).

---

## 16. LuaJIT layer

### 16.1 Binding strategy

FFI cdata, not the C API. Declare the structs to LuaJIT and let Lua fill them directly:

```lua
ui.box({ dir = "row", gap = 8, pad = 12 },
       { bg = theme.surface, radius = 8 })
```

Implementation: a preallocated, reused `ffi.new("vv_LayoutDecl")` and `ffi.new("vv_Style")` per nesting depth, populated from the table, then passed by pointer. Table-to-struct field assignment is the per-widget cost; at a few thousand widgets on LuaJIT this is comfortably sub-millisecond.

If it ever isn't: offer a "compiled style" object (`ui.style{...}` returns a cached cdata) so static styles are filled once, and only dynamic fields are written per frame. Don't build this until measured.

### 16.2 Identity from Lua

Sequence-based IDs (§3.1) work identically in Lua with no `debug.getinfo` cost. Explicit keys via the `key` field. This was the main reason for choosing sequence over call-site hashing.

### 16.3 Hot reload

This is the feature that justifies the whole stack.

Because the UI is rebuilt every frame from a script, and because *all* state — scroll offsets, spring velocities, text cursors, focus — lives in the C node pool keyed by ID, the script can be re-executed at any time without losing anything. Save the file; the UI morphs in place, mid-animation.

Mechanics:

- Watch the script file (or a directory) for mtime changes.
- On change, load the chunk. **If it fails to compile, keep the previous chunk** and display the error as an overlay.
- If it compiles but throws at runtime, catch via `pcall`, revert to the previous chunk, show the error, and keep running.
- Never let a script error take down the process. The whole value is that experimentation is consequence-free.

**State must survive the reload**, which means module-level Lua locals cannot hold it — the chunk re-executes and resets them. Hence a named persistent store held in the C context:

```lua
local s = ui.store("counter", { count = 0 })
```

`ui.store` returns the same table on every reload, creating it from the defaults only on first call. This is the most important function in the Lua layer: without it, hot reload resets the application on every keystroke and the feature is worthless. It also carries the write barrier for invalidation (§4.3).

The GC hazard originally anticipated here — C holding raw pointers into Lua-owned memory — **does not arise.** Lua widgets use value-in/value-out rather than pointer binding, so the core never retains a reference into the Lua heap past the frame boundary. Pointer discipline (`vv_Value.ptr`, §12) remains a C-only concern.

### 16.4 Coroutines for sequencing

```lua
ui.spawn(function()
  ui.animate(panel, { x = 200 })
  ui.wait(0.2)
  ui.animate(panel, { opacity = 0 })
end)
```

A scheduler resumed in `vv_begin_frame` advances coroutines whose wait has elapsed. This gives imperative animation choreography that reads top-to-bottom, which no C API can match ergonomically, and is a genuine reason to prefer Lua as the primary authoring surface over C.

Design question worth settling early: **is Lua the primary authoring surface with C as the embedding layer, or is C primary with Lua as a binding?** The hot reload and coroutine story argues strongly for the former. That would mean designing the C API for *embeddability and completeness* while designing the Lua API for *ergonomics*, and accepting they need not mirror each other.

---

## 17. Build order

| Phase | Content | Validates |
|---|---|---|
| 0 | Arenas, IDs, node pool, reconciliation, raylib backend drawing flat rects. **Build/Present split from the start.** | Identity is stable |
| 1 | Layout engine: 4-pass, all sizing modes, row/col/gap/pad/align | Layout is correct and cheap |
| 2 | SDF shader: radius, border, shadow, AA, single batched draw | Rendering path is fast and pretty |
| 3 | Springs, Oklab, style interpolation, FLIP, enter/exit, **Present-tier animation** | **The core bet** |
| 4 | Input: hit test, hover/active/focus, capture, springy scroll, `disabled` | It feels good |
| 5 | Text: atlas, measurement, wrapping, height-for-width | Layout handles real content |
| 6 | Declarative style variants; widget set: buttons through scroll areas | It's usable and idle-ready |
| 7 | LuaJIT bindings, `ui.store`, hot reload, coroutine scheduler | **The workflow bet** |
| 8 | Text editing via stb_textedit, selection, clipboard | Feature-complete for v1 |
| 9 | Virtualization (`vv_rows`), value bindings, transactional edit, drag_number, knob, xy_pad, curve editor | It suits visualizers |
| 10 | Idle mode + invalidation (write barrier, `VV_SET`) — opt-in | Idle power |
| 11 | SDL3 backend | The abstraction was real |

Phase 3 is the risky one and should be reached fast — it's where the architecture either pays off or doesn't. Consider building a deliberately ugly Phase 0–2 to get there sooner.

---

## 18. Risks

**Identity instability (highest).** Conditional siblings silently breaking animation continuity will be the most-reported bug and the hardest to diagnose, because the symptom (an element flickers or fails to animate) is far from the cause. Mitigate with the debug churn detector (§3.1) and by making explicit keys idiomatic from the first example in the docs.

**Animation obscuring bugs.** When everything glides, a layout error looks like a transition. Provide a global `vv_set_animation_scale(0.0)` kill switch, and use it in tests.

**The one-frame lag becoming visible.** More likely here than in a static IM library. §4 covers mitigations; monitor whether they suffice.

**Shadow overdraw.** §9.3.

**Widget-set scope creep.** The framework is the product. Every hour spent on a date picker is an hour not spent on the thing that makes this library worth existing.

**Hot reload state loss.** If `ui.store` is not in place before Phase 7 ships, hot reload resets the application on every save and the headline feature is worthless. §16.3.

**Idle mode turning every bug into an invalidation question.** The reason it defaults off (§4.2). If it ever becomes the default, the debugging cost lands on users rather than on you.

**Phase separation eroding.** Build and Present are cleanly separable at Phase 0 and it would be easy to leak — a Present-tier step that quietly needs a layout value, or a widget that mutates the tree during emit. Once entangled, idle mode becomes a refactor rather than a feature. Enforce with an assert: during Present, the node pool must not allocate or free.

**Culling colliding with exit animations.** §5.5. A scroll that leaves fading corpses in its wake is an obvious bug but an easy one to ship.

---

## 19. Testing

- **Layout golden tests.** Serialize the post-layout node tree (id, rect, sizing mode) to JSON; diff against committed expectations. Catches regressions precisely, with no rendering involved.
- **Deterministic mode.** Fixed `dt`, `animation_scale = 0`, so springs snap instantly and every frame is reproducible.
- **Reconciliation fuzzing.** Randomly mutate a tree structure across frames; assert no node leaks, no ID collisions, that every exiting node eventually frees, and that `widget_state` is never orphaned.
- **Perf harness.** 10k nodes, measure per-phase timings. Layout and text measurement will dominate; know the numbers before optimizing.
- **Visual regression.** Render to texture, compare against reference PNGs with a perceptual threshold. Brittle, but catches shader regressions nothing else will.

---

## 20. Open questions

1. ~~**Is Lua or C the primary authoring surface?**~~ **Settled: Lua is the authoring surface, C is the embedding and extension layer.** The APIs deliberately diverge — Lua uses value-in/value-out and returns interaction records; C uses pointers, IDs, and query functions. Three concrete divergences are now documented (§12, §14.3, §16.1). C remains complete enough to write every built-in widget (§14.1), which is the constraint that keeps it honest.

1a. **Should style variants be sparse-with-mask or full-struct-with-sentinel?** (§7.1) The zero-means-inherit convention breaks down for variants. A presence bitfield is more code but unambiguous; a sentinel color value is simpler but leaks an unrepresentable value. Decide before Phase 6.

1b. **Does `vv_rows` need a `VV_KEEP_STATE` variant?** (§5.5) Retaining widget state for culled rows costs unbounded memory but preserves scroll-back behaviour for things like partially-filled text fields in a long form. Probably yes, probably not v1.
2. **Multi-window.** Out of scope for v1, but does the context own the window or does the app? Getting this wrong makes it hard to add later. Recommend: the app owns windows, the context is per-window.
3. **DPI.** Everything in logical pixels with a scale factor at the backend, or physical pixels throughout? Recommend logical; it makes multi-monitor sane.
4. **Does `PATH` belong in the command set?** Deferred in §8. Adding vector paths would let plot widgets draw curves through the same pipeline instead of through `CUSTOM`. Worth revisiting at Phase 9.
5. **Undo/redo scope.** Text editing has its own via stb_textedit. A global undo stack needs the value registry (§12) — decide whether that's in scope before Phase 9.
