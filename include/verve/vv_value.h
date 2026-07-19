// vv_value.h — lightweight value bindings (§12).
//
// Deliberately minimal now, structured so a full parameter system (presets,
// undo, automation, MIDI/OSC) is an addition rather than a refactor. In v1 only
// min/max/curve are read; name/unit/flags exist and are ignored, so widget
// signatures never need to change when the registry lands.
#ifndef VV_VALUE_H
#define VV_VALUE_H

#include "vv_event.h"
#include "vv_types.h"
#include <math.h>

typedef enum { VV_VAL_F32, VV_VAL_I32, VV_VAL_BOOL, VV_VAL_COLOR, VV_VAL_STR } vv_ValueKind;

typedef enum {
    VV_VAL_AUTOMATABLE = 1u << 0,
    VV_VAL_PRESET      = 1u << 1,
    VV_VAL_READONLY    = 1u << 2,
    VV_VAL_LOG         = 1u << 3,
} vv_ValueFlags;

typedef struct {
    const char *name;
    const char *unit;
    float       min, max;
    float       curve;   // 1.0 linear; >1 skews toward min
    uint32_t    flags;
} vv_ValueMeta;

typedef struct {
    vv_ValueKind        kind;
    void               *ptr;    // points at user memory
    const vv_ValueMeta *meta;   // nullable in v1
} vv_Value;

static inline vv_Value vv_f32(float *p, const vv_ValueMeta *m) {
    return (vv_Value){ VV_VAL_F32, p, m };
}
static inline vv_Value vv_i32(int32_t *p, const vv_ValueMeta *m) {
    return (vv_Value){ VV_VAL_I32, p, m };
}
static inline vv_Value vv_boolval(bool *p, const vv_ValueMeta *m) {
    return (vv_Value){ VV_VAL_BOOL, p, m };
}

// Read a bound target as a float regardless of its stored kind, so numeric
// widgets (slider/drag) display the right value even for an int32 target (whose
// bits must not be reinterpreted as a float).
static inline float vv_value_as_float(vv_Value v) {
    if (!v.ptr) return 0.0f;
    switch (v.kind) {
    case VV_VAL_I32:  return (float)*(int32_t *)v.ptr;
    case VV_VAL_BOOL: return *(bool *)v.ptr ? 1.0f : 0.0f;
    default:          return *(float *)v.ptr; // F32
    }
}

// ---- two-way binding over the message queue (§12) --------------------------
// A value-bound widget doesn't write its target directly (that would mutate
// state during view). Instead it emits VV_MSG_BIND carrying this record; the
// driver applies it via vv_apply() before update() runs. The single apply site
// is where undo/automation (§12.1) will hook in.
typedef struct {
    vv_Value   target;
    vv_Payload val;   // new value, kind matching target.kind
} vv_BindEvent;

// Write a VV_MSG_BIND event's new value through its target pointer. Called by
// the driver; exposed so apps driving their own loop can apply bind events too.
void vv_apply(vv_Event ev);

// Transactional editing (§12.1): one undo entry per drag session, not per
// frame. Bound drag widgets call begin on press and end on release. ctx is
// forward-declared to keep this header free of the context dependency.
typedef struct vv_Ctx vv_Ctx;
void vv_begin_edit(vv_Ctx *ctx, vv_Value v);
void vv_end_edit(vv_Ctx *ctx, vv_Value v);

// Map value<->normalized [0,1] honoring min/max and a perceptual curve
// (curve 1 = linear; >1 gives finer control near min). meta may be NULL.
static inline float vv_value_norm(const vv_ValueMeta *meta, float lo, float hi, float v) {
    if (hi <= lo) return 0.0f;
    float t = (v - lo) / (hi - lo);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float curve = meta && meta->curve > 0 ? meta->curve : 1.0f;
    return curve == 1.0f ? t : powf(t, 1.0f / curve);
}
static inline float vv_value_denorm(const vv_ValueMeta *meta, float lo, float hi, float t) {
    float curve = meta && meta->curve > 0 ? meta->curve : 1.0f;
    float tt = curve == 1.0f ? t : powf(t, curve);
    return lo + (hi - lo) * tt;
}

#endif // VV_VALUE_H
