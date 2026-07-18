// vv_event.h — messages, payloads, and the event queue (§ message-driven UI).
//
// Verve follows a message/update/view loop (The Elm Architecture). Widgets do
// not return interaction values; instead they EMIT a message + payload into a
// per-context queue when an interaction fires (click, change, hover, ...). The
// application drains that queue into its own update(state, event) function, and
// re-runs view(ctx, state) only when something changed. This keeps application
// logic (update) and view logic (view) cleanly separated: view never mutates
// state, update never touches the UI tree.
//
//   view()  emits  ->  [ queue ]  ->  drained into  update()  ->  mutates state
//
// A message is a user-defined integer id (VV_MSG_NONE == 0 is reserved to mean
// "no message"), wide enough to never collide. The payload is a small union so
// a checkbox can carry its new bool, a slider its new float, a list row a ptr.
#ifndef VV_EVENT_H
#define VV_EVENT_H

#include "vv_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t vv_Msg;
#define VV_MSG_NONE ((vv_Msg)0)

// Reserved message emitted by value-bound widgets (§12). Its payload is a
// vv_BindEvent* (frame-arena) and vv_run_frame applies it automatically before
// dispatching to update(), so bound widgets need no update() case. User message
// ids must avoid this sentinel (it sits at the top of the range).
#define VV_MSG_BIND ((vv_Msg)UINT64_MAX)

typedef union {
    int64_t     as_int;
    double      as_float;
    void       *as_ptr;
    const char *as_str;
} vv_Payload;

// Payload constructors — terse call sites: vv_emit(c, MSG, vv_pi(-1)).
static inline vv_Payload vv_pi(int64_t v)     { return (vv_Payload){ .as_int = v }; }
static inline vv_Payload vv_pf(double v)      { return (vv_Payload){ .as_float = v }; }
static inline vv_Payload vv_pp(void *v)       { return (vv_Payload){ .as_ptr = v }; }
static inline vv_Payload vv_ps(const char *v) { return (vv_Payload){ .as_str = v }; }
#define VV_NO_PAYLOAD ((vv_Payload){ 0 })

// A 2D point packs into the 64-bit payload (two float32s), so a move/click event
// carries the cursor position by value — no allocation, no lifetime worry.
static inline vv_Payload vv_pv2(vv_Vec2 v) {
    uint32_t x, y;
    memcpy(&x, &v.x, sizeof x);
    memcpy(&y, &v.y, sizeof y);
    return (vv_Payload){ .as_int = (int64_t)(((uint64_t)x << 32) | y) };
}
static inline vv_Vec2 vv_as_v2(vv_Payload p) {
    uint32_t x = (uint32_t)((uint64_t)p.as_int >> 32), y = (uint32_t)p.as_int;
    vv_Vec2 v;
    memcpy(&v.x, &x, sizeof v.x);
    memcpy(&v.y, &y, sizeof v.y);
    return v;
}

typedef struct {
    vv_Msg     msg;
    vv_Payload data;
} vv_Event;

// Optional per-interaction bindings passed to a widget's *_on variant. Any
// field left VV_MSG_NONE is not emitted. The widget's primary action (a
// button's click, a slider's change) is a direct parameter, not part of this.
typedef struct {
    vv_Msg hover; // pointer entered the widget this frame
    vv_Msg press; // pressed down on the widget this frame
    vv_Msg dbl;   // double-clicked this frame
    vv_Msg move;  // pointer moved while over the widget (payload = cursor pos).
                  // Only a widget that sets this rebuilds on plain motion; the
                  // event carries vv_pv2(mouse), read back with vv_as_v2.
} vv_On;

#endif // VV_EVENT_H
