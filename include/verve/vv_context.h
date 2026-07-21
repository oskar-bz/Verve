// vv_context.h — the frame lifecycle and immediate-mode build API (§4).
//
// User code declares the TARGET for the current logical state; the retained
// tree holds the ACTUAL value. begin_frame/build/end_frame is the whole loop.
#ifndef VV_CONTEXT_H
#define VV_CONTEXT_H

#include "vv_arena.h"
#include "vv_command.h"
#include "vv_event.h"
#include "vv_id.h"
#include "vv_node.h"
#include "vv_str.h"

// Editing/navigation keys routed to the focused node (§11.3). Printable text
// arrives separately as UTF-8 in vv_Input.text.
typedef enum {
    VV_KEY_NONE = 0,
    VV_KEY_LEFT, VV_KEY_RIGHT, VV_KEY_UP, VV_KEY_DOWN,
    VV_KEY_HOME, VV_KEY_END, VV_KEY_BACKSPACE, VV_KEY_DELETE,
    VV_KEY_ENTER, VV_KEY_TAB, VV_KEY_ESCAPE,
    VV_KEY_A, VV_KEY_C, VV_KEY_V, VV_KEY_X, VV_KEY_Z,
} vv_Key;

typedef struct { uint16_t key; bool shift, ctrl; } vv_KeyEvent;

#define VV_INPUT_TEXT_CAP 32
#define VV_INPUT_KEY_CAP  16

typedef struct {
    vv_Vec2 mouse;
    bool    mouse_down;   // left button
    bool    right_down;   // right button (context menus)
    float   wheel;

    char        text[VV_INPUT_TEXT_CAP];  // UTF-8 committed this frame
    uint8_t     text_len;
    char        preedit[64];              // IME composition in progress (§10, not committed)
    uint8_t     preedit_len;
    vv_KeyEvent keys[VV_INPUT_KEY_CAP];    // discrete key presses this frame
    uint8_t     key_count;
    bool        shift, ctrl, alt;
} vv_Input;

// Frame tier actually executed (§4.2). Reported for diagnostics/tests.
typedef enum { VV_TIER_IDLE, VV_TIER_PRESENT, VV_TIER_BUILD } vv_FrameTier;

#define VV_BUILD_STACK_MAX 256

#define VV_EVENT_CAP 256

typedef struct vv_Ctx {
    vv_Arena    persistent;
    vv_Arena    frame;    // build scratch + node text; reset only on build frames
    vv_Arena    present;  // command-buffer items; reset every frame
    vv_NodePool pool;

    uint64_t frame_index;
    float    dt;
    vv_Input input;
    float    win_w, win_h, dpi_scale;
    float    animation_scale;   // §18 kill switch; 1.0 normal, 0.0 = snap
    vv_SpringParams default_spring; // fallback spring feel for nodes without an override

    // Backend text measurement, used during layout (§8.1, §15). NULL => the
    // built-in monospace estimate stands in.
    vv_Vec2 (*measure_text)(void *ud, const char *s, int len,
                            vv_FontID font, float size, float wrap_width);
    void    *measure_ud;

    // Backend clipboard (§10.3), NULL until bound. Editors route copy/cut/paste
    // through these so the core stays backend-free.
    const char *(*clipboard_get)(void *ud);
    void        (*clipboard_set)(void *ud, const char *s);
    void        *clipboard_ud;

    // Cursor the hovered node requests (§11); the backend applies it each frame.
    vv_CursorShape cursor;

    bool     idle_mode;         // §4.2 opt-in
    bool     tree_dirty;        // forces a Build tier next frame
    uint32_t unsettled_springs; // Present-tier gate
    uint32_t unsettled_rects;   // rect (FLIP) springs still moving — forces a
                                // rebuild so widgets that read a parent's
                                // actual_rect at build time (slider fill, tab
                                // indicator, popover anchors) track the animation

    uint32_t root;              // pool index of the root node

    // Input state (§11). IDs, not indices, so they survive pool churn. Hit
    // testing runs at begin_frame against last frame's actual_rect, so build
    // code queries current mouse vs. what the user saw (the §4.5 one-frame lag).
    vv_ID   hovered_id;
    vv_ID   active_id;     // pointer capture: survives leaving the rect (§11.2)
    vv_ID   focused_id;    // keyboard target (§11.1)
    vv_ID   pressed_id;    // went down this frame
    vv_ID   clicked_id;    // down+up both inside this frame
    vv_ID   double_clicked_id; // second click within the double-click window
    vv_ID   right_active_id;   // node captured by a right-button press
    vv_ID   right_clicked_id;  // right down+up both inside this frame
    bool    mouse_prev_down;
    bool    mouse_released;    // release edge this frame, survives into view()
                              // (mouse_prev_down is overwritten before view runs)
    bool    mouse_right_prev;
    vv_Vec2 mouse_prev;   // last frame's pointer, to detect movement (idle gate)
    vv_Vec2 drag_start;
    vv_Vec2 drag_delta;
    vv_ID   sb_drag;      // scroll container whose scrollbar thumb is being dragged

    // Drag-and-drop: a payload carried from a drag source to a drop target while
    // the button is held. Cleared at end-of-frame once the button is released.
    bool      dnd_dragging;
    vv_Payload dnd_payload;
    vv_ID     dnd_source;
    float   sb_grab;      // pointer y minus thumb top at grab, for 1:1 tracking
    bool    focus_next;   // autofocus the next focusable node built

    // Message queue (§ message-driven UI). Emitted during view(), drained by the
    // driver into update() next frame. A fixed ring; survives the frame reset.
    vv_Event events[VV_EVENT_CAP];
    uint32_t event_head, event_count;

    float    clock;       // seconds accumulated from dt, for click timing
    float    last_click_time;
    vv_ID    last_click_id;
    bool     wants_build; // this frame's input can change the tree (idle gate)

    // Transactional edit session (§12.1): one undo entry per drag, not per
    // frame. begin_edit snapshots; end_edit commits (bumps edit_generation).
    const void *edit_ptr;      // target of the active edit, NULL if none
    uint32_t    edit_generation; // increments once per committed edit session

    // Build-time stack.
    uint32_t stack[VV_BUILD_STACK_MAX];
    uint32_t seq_counter[VV_BUILD_STACK_MAX];
    int      depth;
    bool     in_build;

    // Transient UI-local state, keyed by string, not tied to a node's lifetime
    // (§12.2). For view-local flags — "is this popover open", a hover index —
    // that would otherwise need an app-state field + message + update case.
    struct { vv_ID id; void *data; } ui_locals[128];
    int      ui_local_count;

    // Overlay layer: nodes with decl.z > 0 collected during present and painted
    // last, in ascending z, above the normal tree (§ overlays).
    uint32_t overlays[64];
    int      overlay_count;
    bool     emitting_overlay; // guard so an overlay root isn't re-skipped

    vv_CommandBuffer cmds;

    vv_FrameTier last_tier;
} vv_Ctx;

void vv_init(vv_Ctx *ctx);
void vv_shutdown(vv_Ctx *ctx);

void vv_set_window(vv_Ctx *ctx, float w, float h, float dpi_scale);
void vv_set_measure_fn(vv_Ctx *ctx,
                       vv_Vec2 (*fn)(void *ud, const char *s, int len,
                                     vv_FontID font, float size, float wrap_width),
                       void *ud);

// Route editor copy/cut/paste through the backend's clipboard (§10.3). Bind once
// at startup; widgets use vv_clipboard_get/set. Unbound => copy/paste are no-ops.
void vv_set_clipboard_fns(vv_Ctx *ctx, const char *(*get)(void *ud),
                          void (*set)(void *ud, const char *s), void *ud);
const char *vv_clipboard_get(vv_Ctx *ctx);
void        vv_clipboard_set(vv_Ctx *ctx, const char *s);

// The cursor shape requested by the hovered node this frame (§11). The host
// applies it to its window each frame (e.g. vv_app_set_cursor).
vv_CursorShape vv_cursor(const vv_Ctx *ctx);
void vv_set_idle_mode(vv_Ctx *ctx, bool on);
void vv_set_animation_scale(vv_Ctx *ctx, float scale);
// Default spring feel (response seconds, damping) for every node that doesn't
// set vv_Style.spring itself. Lets an app tune global motion at runtime.
void vv_set_default_spring(vv_Ctx *ctx, vv_SpringParams params);
void vv_invalidate(vv_Ctx *ctx);

void vv_begin_frame(vv_Ctx *ctx, float dt, const vv_Input *input);
vv_CommandBuffer *vv_end_frame(vv_Ctx *ctx);

// ---- message loop (§ message-driven UI) ----------------------------------
// The blessed path. Widgets built in `view` emit messages; this driver drains
// last frame's messages into `update`, then rebuilds `view` only if a message
// was processed or this frame's input could produce one — otherwise it just
// presents (advancing animations). One rebuild per frame (one-frame pipeline);
// the policy lives entirely here so it can later become a settle loop without
// touching application code.
//
// Returns the command buffer to hand to the backend. Returns NULL when the
// frame is fully idle (idle mode on, animations settled, no input) — nothing
// changed on screen, so the caller should skip drawing and buffer-swapping for
// that frame. Enable idle mode with vv_set_idle_mode to get ~0% CPU on a static
// UI; with it off, a valid buffer is returned every frame.
typedef void (*vv_UpdateFn)(void *state, vv_Event ev);
typedef void (*vv_ViewFn)(vv_Ctx *ctx, void *state);

vv_CommandBuffer *vv_run_frame(vv_Ctx *ctx, float dt, const vv_Input *input,
                               vv_UpdateFn update, vv_ViewFn view, void *state);

// Low-level queue access, for apps that drive their own loop.
void vv_emit(vv_Ctx *ctx, vv_Msg msg, vv_Payload data);
bool vv_poll_event(vv_Ctx *ctx, vv_Event *out); // FIFO; false when empty

// ---- tree building -------------------------------------------------------

// Open a container. Returns its pool index (opaque handle for queries).
// `key`/`klen` may be NULL/0 to rely on sequence identity (§3.1).
uint32_t vv_box_keyed(vv_Ctx *ctx, const char *key, size_t klen,
                      vv_LayoutDecl decl, vv_Style style);
void     vv_end_box(vv_Ctx *ctx);

// Leaf text node.
uint32_t vv_text_keyed(vv_Ctx *ctx, const char *key, size_t klen,
                       const char *utf8, vv_Style style);

// Custom-draw leaf (§14.3): a box laid out by `decl` whose content is rendered
// by the backend calling `draw->fn(draw->ud, rect)` — an escape hatch to raw GPU
// drawing (a viewport, a plot, a scene) inside the reconciled tree. `draw` must
// outlive the frame (point at app-persistent memory, not the frame arena). The
// node is a normal leaf otherwise: it hit-tests, sizes, and animates its rect.
uint32_t vv_custom(vv_Ctx *ctx, const char *key, size_t klen,
                   const vv_CustomDraw *draw, vv_LayoutDecl decl);

// Convenience wrappers with no explicit key.
static inline uint32_t vv_box(vv_Ctx *ctx, vv_LayoutDecl d, vv_Style s) {
    return vv_box_keyed(ctx, NULL, 0, d, s);
}
static inline uint32_t vv_text(vv_Ctx *ctx, const char *utf8, vv_Style s) {
    return vv_text_keyed(ctx, NULL, 0, utf8, s);
}

// Virtualized rows (§5.5). Consults the enclosing scroll viewport, builds only
// the visible index range, and emits spacers above/below. Rows scrolled out of
// view are freed immediately without an exit animation (no fading corpses in a
// scroll). The tradeoff is identity: culled rows lose animation continuity and
// retained widget state — right for a spreadsheet, wrong for animating cards.
// `fn` fills one row's contents; the row box itself is provided.
void vv_rows(vv_Ctx *ctx, int count, float row_height,
             void (*fn)(vv_Ctx *ctx, int index, void *ud), void *ud);

// Scoped container: `VV_BOX(ctx, decl, style) { ...children... }`.
// The loop variable is line-unique so nested VV_BOX blocks don't shadow.
#define VV_CAT_(a, b) a##b
#define VV_CAT(a, b)  VV_CAT_(a, b)
#define VV_BOX(ctx, decl, style)                                            \
    for (int VV_CAT(_vv_once_, __LINE__) = (vv_box((ctx), (decl), (style)), 1); \
         VV_CAT(_vv_once_, __LINE__);                                       \
         VV_CAT(_vv_once_, __LINE__) = (vv_end_box(ctx), 0))

// Access a node by handle (valid only within the current frame).
static inline vv_Node *vv_node(vv_Ctx *ctx, uint32_t index) {
    return vv_pool_get(&ctx->pool, index);
}

// ---- interaction queries (§11.1) -----------------------------------------
// All take a node handle from a build call this frame. Reflect the hit test
// run at begin_frame (current pointer vs. last frame's geometry, §4.5).

bool    vv_hovered(vv_Ctx *ctx, uint32_t index);
bool    vv_pressed(vv_Ctx *ctx, uint32_t index);  // went down this frame
bool    vv_clicked(vv_Ctx *ctx, uint32_t index);  // down+up both inside
bool    vv_active(vv_Ctx *ctx, uint32_t index);   // held with capture
bool    vv_focused(vv_Ctx *ctx, uint32_t index);
bool    vv_double_clicked(vv_Ctx *ctx, uint32_t index);
bool    vv_right_clicked(vv_Ctx *ctx, uint32_t index); // right-button click (context menus)
// True when the node is focused and Enter was pressed this frame — so keyboard
// users can activate a Tab-focused button/checkbox/menu item (§11.3).
bool    vv_activated(vv_Ctx *ctx, uint32_t index);
vv_Vec2 vv_drag_delta(vv_Ctx *ctx, uint32_t index);
void    vv_focus(vv_Ctx *ctx, uint32_t index);    // programmatic focus

// Drag-and-drop. vv_drag_source begins carrying `payload` once a pressed node is
// dragged past a small threshold; returns true while it is the active source.
// vv_drop_target writes the payload to `*out` and returns true (once) when the
// drag releases over it. vv_dnd_active reports whether any drag is in progress.
bool vv_drag_source(vv_Ctx *ctx, uint32_t index, vv_Payload payload);
bool vv_drop_target(vv_Ctx *ctx, uint32_t index, vv_Payload *out);
bool vv_dnd_active(vv_Ctx *ctx);
// Payload of the in-progress drag, for rendering a pointer-following ghost.
vv_Payload vv_dnd_payload(vv_Ctx *ctx);
// True while a drag is live and the pointer is over this drop target, for a
// landing preview (a placeholder that opens where the payload would drop).
bool vv_drop_hover(vv_Ctx *ctx, uint32_t index);
// Focus the next focusable node built this frame (autofocus a field on open).
void    vv_request_focus_next(vv_Ctx *ctx);

// ---- widget authoring (§14.2) --------------------------------------------
// Persistent, zeroed-on-first-use per-node state, freed when the node dies.
// The one genuinely privileged widget call.
void   *vv_state_raw(vv_Ctx *ctx, uint32_t index, size_t size);
#define vv_state(ctx, index, T) ((T *)vv_state_raw((ctx), (index), sizeof(T)))

// Transient UI-local state, keyed by a string rather than a node (§12.2). Zeroed
// on first use, persists across frames for the session — the escape hatch for
// small view-local flags (popover open, hovered index, a draft value) so they
// don't need a field in app state plus a message and an update() case. Keep the
// key unique and stable; the set of keys is expected to be bounded (cap 128).
void   *vv_ui_state_raw(vv_Ctx *ctx, const char *key, size_t size);
#define vv_ui_state(ctx, key, T) ((T *)vv_ui_state_raw((ctx), (key), sizeof(T)))

// Current pointer position (screen space), for widgets doing their own math.
static inline vv_Vec2 vv_mouse(vv_Ctx *ctx) { return ctx->input.mouse; }

// Format a string into the frame arena for immediate use in build code:
//   vv_label(c, vv_fmt(c, "%.1f s", elapsed));
// The result lives until the next frame — long enough for the text widget to
// copy it — so no scratch buffer and no snprintf sizing at the call site.
vv_Str vv_fmt(vv_Ctx *ctx, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#endif // VV_CONTEXT_H
