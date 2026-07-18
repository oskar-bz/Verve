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

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t vv_Msg;
#define VV_MSG_NONE ((vv_Msg)0)

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
} vv_On;

#endif // VV_EVENT_H
