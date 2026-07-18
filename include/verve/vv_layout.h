// vv_layout.h — layout declaration types (§5).
//
// Flexbox vocabulary (rows, columns, gaps, grow) on Flutter-style box
// constraints. The 4-pass engine lives in vv_layout.c (Phase 1); this header is
// the declaration surface user code fills in per frame.
#ifndef VV_LAYOUT_H
#define VV_LAYOUT_H

#include "vv_types.h"

typedef enum { VV_ROW, VV_COLUMN } vv_Axis;

typedef enum {
    VV_SIZE_FIT = 0,   // intrinsic content size, clamped (default => zero-init)
    VV_SIZE_FIXED,     // exactly `value` logical pixels
    VV_SIZE_GROW,      // share of leftover main-axis space, weight `value`
    VV_SIZE_PERCENT,   // fraction of parent's resolved size on this axis
} vv_SizeMode;

typedef struct {
    vv_SizeMode mode;
    float       value;      // n, weight, or fraction
    float       min, max;   // clamps applied in all modes (0 max => +inf)
} vv_Size;

typedef enum {
    VV_ALIGN_START = 0,
    VV_ALIGN_CENTER,
    VV_ALIGN_END,
    VV_ALIGN_SPACE_BETWEEN, // main axis only
} vv_Align;

typedef struct vv_LayoutDecl {
    vv_Axis  dir;           // ROW | COLUMN
    vv_Size  w, h;
    vv_Edges padding;       // l, t, r, b
    float    gap;
    vv_Align main, cross;
    bool     wrap;
    bool     clip;
    bool     scroll_x, scroll_y;
    bool     focusable;     // participates in tab traversal
    bool     disabled;      // suppresses hit test + focus; inherited
    bool     has_absolute;  // if true, `absolute` escapes flow
    vv_Rect  absolute;      // tooltips, popovers, drags
    int      z;             // layer for popovers/overlays
    float    aspect_ratio;  // >0 locks h from w (escape hatch, §5.3)
} vv_LayoutDecl;

// ---- terse size constructors --------------------------------------------

static inline vv_Size vv_fixed(float n)   { return (vv_Size){VV_SIZE_FIXED, n, 0, 0}; }
static inline vv_Size vv_fit(void)        { return (vv_Size){VV_SIZE_FIT, 0, 0, 0}; }
static inline vv_Size vv_grow(float w)    { return (vv_Size){VV_SIZE_GROW, w, 0, 0}; }
static inline vv_Size vv_percent(float p) { return (vv_Size){VV_SIZE_PERCENT, p, 0, 0}; }

#endif // VV_LAYOUT_H
