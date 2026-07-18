#include "verve/vv_anim.h"

#include <math.h>

static const float DEFAULT_EPS = 0.001f;

void vv_spring_init(vv_Spring *s, float value, vv_SpringParams p) {
    s->x        = value;
    s->v        = 0.0f;
    s->target   = value;
    s->response = p.response > 0.0001f ? p.response : 0.25f;
    s->damping  = p.damping;
    s->eps      = DEFAULT_EPS;
    s->settled  = true;
}

void vv_spring_retarget(vv_Spring *s, float target) {
    if (target != s->target) {
        s->target  = target;
        s->settled = false;
    }
}

void vv_spring_step(vv_Spring *s, float dt) {
    if (s->settled || dt <= 0.0f) return;

    const float omega = 6.2831853f / s->response;
    int   steps = (int)(dt / (1.0f / 240.0f)) + 1;
    float h     = dt / (float)steps;
    for (int i = 0; i < steps; i++) {
        float a = -omega * omega * (s->x - s->target) - 2.0f * s->damping * omega * s->v;
        s->v += a * h;
        s->x += s->v * h;
    }
    if (fabsf(s->x - s->target) < s->eps && fabsf(s->v) < s->eps * 10.0f) {
        s->x       = s->target;
        s->v       = 0.0f;
        s->settled = true;
    }
}

void vv_spring_snap(vv_Spring *s) {
    s->x       = s->target;
    s->v       = 0.0f;
    s->settled = true;
}
