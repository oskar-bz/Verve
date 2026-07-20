// vv_types.h — fundamental value types shared across the whole core.
// Pure data + inline helpers; no allocation, no state. Everything here is
// trivially copyable and safe to store by value in nodes and commands.
#ifndef VV_TYPES_H
#define VV_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t vv_ID;

typedef struct { float x, y; } vv_Vec2;
typedef struct { float x, y, w, h; } vv_Rect;

// RGBA, straight-alpha, components in [0,1]. sRGB unless noted.
typedef struct { float r, g, b, a; } vv_Color;

// Per-side (left, top, right, bottom) — padding, border widths, margins.
typedef struct { float l, t, r, b; } vv_Edges;

// Per-corner radii (top-left, top-right, bottom-right, bottom-left).
typedef struct { float tl, tr, br, bl; } vv_Corners;

// 2x3 affine transform, column-major: [a c tx; b d ty].
typedef struct { float a, b, c, d, tx, ty; } vv_Mat23;

typedef struct {
    vv_Color color;
    vv_Vec2  offset;
    float    blur;
    float    spread;
    bool     inset;
} vv_Shadow;

typedef uint32_t vv_FontID;
typedef uint32_t vv_TexID;

// Mouse cursor shape a node can request (§11); the backend applies it.
typedef enum {
    VV_CURSOR_DEFAULT,
    VV_CURSOR_POINTER,
    VV_CURSOR_TEXT,
    VV_CURSOR_RESIZE_H,
    VV_CURSOR_RESIZE_V,
} vv_CursorShape;

// A custom-draw escape hatch (§14.3): `fn` is called by the backend with the
// node's on-screen rect (logical coords) to do arbitrary rendering there — a
// GPU viewport, a plot, a scene. The backend scissors to the rect and restores
// its own state afterward. `ud` is the app's data. See vv_custom().
typedef struct { void (*fn)(void *ud, vv_Rect rect); void *ud; } vv_CustomDraw;

// ---- constructors (terse, designed for call-site use) --------------------

static inline vv_Vec2  vv_v2(float x, float y)            { return (vv_Vec2){x, y}; }
static inline vv_Rect  vv_rect(float x, float y, float w, float h) { return (vv_Rect){x, y, w, h}; }
static inline vv_Color vv_rgba(float r, float g, float b, float a) { return (vv_Color){r, g, b, a}; }
static inline vv_Color vv_rgb(float r, float g, float b) { return (vv_Color){r, g, b, 1.0f}; }
static inline vv_Edges vv_all(float n)                   { return (vv_Edges){n, n, n, n}; }
static inline vv_Edges vv_hv(float h, float v)           { return (vv_Edges){h, v, h, v}; }
static inline vv_Corners vv_r(float n)                   { return (vv_Corners){n, n, n, n}; }
static inline vv_Mat23 vv_mat_identity(void)             { return (vv_Mat23){1, 0, 0, 1, 0, 0}; }
static inline vv_Mat23 vv_scale(float s)                 { return (vv_Mat23){s, 0, 0, s, 0, 0}; }

// ---- small helpers -------------------------------------------------------

static inline float vv_minf(float a, float b) { return a < b ? a : b; }
static inline float vv_maxf(float a, float b) { return a > b ? a : b; }
static inline float vv_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static inline float vv_lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Map x from [in_lo, in_hi] onto [out_lo, out_hi] (no clamping). Guards a
// zero-width input range by returning out_lo — the natural "no information" fallback.
static inline float vv_remapf(float x, float in_lo, float in_hi, float out_lo, float out_hi) {
    float d = in_hi - in_lo;
    return d == 0.0f ? out_lo : out_lo + (out_hi - out_lo) * ((x - in_lo) / d);
}

static inline vv_Vec2 vv_v2add(vv_Vec2 a, vv_Vec2 b)   { return (vv_Vec2){a.x + b.x, a.y + b.y}; }
static inline vv_Vec2 vv_v2sub(vv_Vec2 a, vv_Vec2 b)   { return (vv_Vec2){a.x - b.x, a.y - b.y}; }
static inline vv_Vec2 vv_v2scale(vv_Vec2 a, float s)   { return (vv_Vec2){a.x * s, a.y * s}; }

static inline bool vv_rect_contains(vv_Rect r, vv_Vec2 p) {
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

#endif // VV_TYPES_H
