// vv_value.h — lightweight value bindings (§12).
//
// Deliberately minimal now, structured so a full parameter system (presets,
// undo, automation, MIDI/OSC) is an addition rather than a refactor. In v1 only
// min/max/curve are read; name/unit/flags exist and are ignored, so widget
// signatures never need to change when the registry lands.
#ifndef VV_VALUE_H
#define VV_VALUE_H

#include "vv_types.h"

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

#endif // VV_VALUE_H
