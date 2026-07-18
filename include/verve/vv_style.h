// vv_style.h — style declaration + interpolated actual state (§7).
//
// Zero-init means "inherit from theme". Variants (hover/active/focus/disabled)
// are sparse overlays; because zero is meaningful there, a `set` presence mask
// disambiguates "transparent black" from "unset" (§7.1, open question 1a).
#ifndef VV_STYLE_H
#define VV_STYLE_H

#include "vv_anim.h"
#include "vv_types.h"

// Presence bits for sparse variant overlays.
typedef enum {
    VV_STYLE_BG           = 1u << 0,
    VV_STYLE_FG           = 1u << 1,
    VV_STYLE_RADIUS       = 1u << 2,
    VV_STYLE_BORDER_WIDTH = 1u << 3,
    VV_STYLE_BORDER_COLOR = 1u << 4,
    VV_STYLE_SHADOW       = 1u << 5,
    VV_STYLE_OPACITY      = 1u << 6,
    VV_STYLE_TRANSFORM    = 1u << 7,
    VV_STYLE_FONT         = 1u << 8,
    VV_STYLE_FONT_SIZE    = 1u << 9,
} vv_StyleField;

// Per-property transition control (§6.4).
typedef enum {
    VV_TRANSITION_SPRING = 0, // default: animate
    VV_TRANSITION_INSTANT,    // snap (continuously-driven values, §6.4.1)
} vv_Transition;

typedef struct vv_Style {
    vv_Color   bg;
    vv_Color   fg;            // text
    vv_Corners radius;        // per-corner
    vv_Edges   border_width;  // per-side
    vv_Color   border_color;
    vv_Shadow  shadow;
    float      opacity;
    vv_Mat23   transform;
    vv_FontID  font;
    float      font_size;

    vv_SpringParams spring;   // per-node override (zero => theme/default)
    uint32_t   transition_mask; // vv_StyleField bits to snap instead of spring

    uint32_t   set;           // vv_StyleField bits explicitly provided

    // Declarative state variants (§4.4); NULL = no override.
    const struct vv_Style *hover, *active, *focus, *disabled;
} vv_Style;

// Interpolated actual values with velocities. One spring per animatable
// scalar/color channel. Populated in the Present phase from `target`.
typedef struct vv_StyleAnim {
    vv_Spring bg[4], fg[4], border_color[4], shadow_color[4]; // rgba channels
    vv_Spring radius[4];
    vv_Spring border_width[4];
    vv_Spring opacity;
    vv_Spring scale;   // transform decomposed: uniform scale + rotation
    vv_Spring rotation;
    bool      initialized;
} vv_StyleAnim;

#endif // VV_STYLE_H
