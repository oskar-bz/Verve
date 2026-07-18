// vv_color.h — perceptual color interpolation (§6.3).
//
// Interpolating sRGB gives muddy midpoints; blue->yellow passes through grey.
// We convert sRGB -> linear -> Oklab, interpolate there, and convert back. This
// single choice does an outsized share of making motion look professional.
#ifndef VV_COLOR_H
#define VV_COLOR_H

#include "vv_types.h"

typedef struct { float L, a, b, alpha; } vv_Oklab;

vv_Oklab vv_srgb_to_oklab(vv_Color c);
vv_Color vv_oklab_to_srgb(vv_Oklab o);

// Convenience: interpolate two sRGB colors through Oklab.
vv_Color vv_color_lerp(vv_Color a, vv_Color b, float t);

#endif // VV_COLOR_H
