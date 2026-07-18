// vv_anim.h — springs, the animation primitive (§6.1).
//
// Springs carry velocity, so retargeting mid-flight is the normal case. We
// parametrize by response/damping (tunable by feel) rather than stiffness/mass.
#ifndef VV_ANIM_H
#define VV_ANIM_H

#include "vv_types.h"

typedef struct {
    float x;         // current value
    float v;         // velocity
    float target;    // where we're heading
    float response;  // seconds; roughly the period
    float damping;   // 1.0 = critically damped, <1 bounces
    float eps;       // settle threshold
    bool  settled;
} vv_Spring;

typedef struct {
    float response;
    float damping;
} vv_SpringParams;

// Named presets (§6.1).
#define VV_SNAPPY  ((vv_SpringParams){0.15f, 1.0f})
#define VV_SMOOTH  ((vv_SpringParams){0.35f, 1.0f})
#define VV_BOUNCY  ((vv_SpringParams){0.40f, 0.6f})
#define VV_DEFAULT_SPRING ((vv_SpringParams){0.25f, 1.0f})

// Initialize a spring at rest at `value`.
void vv_spring_init(vv_Spring *s, float value, vv_SpringParams p);

// Retarget without disturbing position/velocity (the graceful-interrupt case).
void vv_spring_retarget(vv_Spring *s, float target);

// Advance by dt. Semi-implicit Euler with substepping (§6.1). Sets `settled`.
void vv_spring_step(vv_Spring *s, float dt);

// Snap instantly to target (animation_scale == 0, VV_INSTANT).
void vv_spring_snap(vv_Spring *s);

#endif // VV_ANIM_H
