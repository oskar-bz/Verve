#include "verve/vv_color.h"

#include <math.h>

// sRGB gamma <-> linear (IEC 61966-2-1).
static float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
static float linear_to_srgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

// Oklab (Björn Ottosson).
vv_Oklab vv_srgb_to_oklab(vv_Color c) {
    float r = srgb_to_linear(c.r), g = srgb_to_linear(c.g), b = srgb_to_linear(c.b);
    float l = 0.4122214708f*r + 0.5363325363f*g + 0.0514459929f*b;
    float m = 0.2119034982f*r + 0.6806995451f*g + 0.1073969566f*b;
    float s = 0.0883024619f*r + 0.2817188376f*g + 0.6299787005f*b;
    float l_ = cbrtf(l), m_ = cbrtf(m), s_ = cbrtf(s);
    return (vv_Oklab){
        .L = 0.2104542553f*l_ + 0.7936177850f*m_ - 0.0040720468f*s_,
        .a = 1.9779984951f*l_ - 2.4285922050f*m_ + 0.4505937099f*s_,
        .b = 0.0259040371f*l_ + 0.7827717662f*m_ - 0.8086757660f*s_,
        .alpha = c.a,
    };
}

vv_Color vv_oklab_to_srgb(vv_Oklab o) {
    float l_ = o.L + 0.3963377774f*o.a + 0.2158037573f*o.b;
    float m_ = o.L - 0.1055613458f*o.a - 0.0638541728f*o.b;
    float s_ = o.L - 0.0894841775f*o.a - 1.2914855480f*o.b;
    float l = l_*l_*l_, m = m_*m_*m_, s = s_*s_*s_;
    float r =  4.0767416621f*l - 3.3077115913f*m + 0.2309699292f*s;
    float g = -1.2684380046f*l + 2.6097574011f*m - 0.3413193965f*s;
    float b = -0.0041960863f*l - 0.7034186147f*m + 1.7076147010f*s;
    return (vv_Color){
        .r = vv_clampf(linear_to_srgb(r), 0, 1),
        .g = vv_clampf(linear_to_srgb(g), 0, 1),
        .b = vv_clampf(linear_to_srgb(b), 0, 1),
        .a = o.alpha,
    };
}

vv_Color vv_color_lerp(vv_Color a, vv_Color b, float t) {
    vv_Oklab oa = vv_srgb_to_oklab(a), ob = vv_srgb_to_oklab(b);
    vv_Oklab o = {
        .L = vv_lerpf(oa.L, ob.L, t),
        .a = vv_lerpf(oa.a, ob.a, t),
        .b = vv_lerpf(oa.b, ob.b, t),
        .alpha = vv_lerpf(oa.alpha, ob.alpha, t),
    };
    return vv_oklab_to_srgb(o);
}
