#include "vv_vector.h"

#include "craz/bake.h"
#include "craz/svg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See vv_vector.h for the design + efficiency model. In short: everything
// rasterizes through craz into a texture uploaded via the backend vtable and is
// drawn through the core's existing image pipeline. Icons are A8 coverage tinted
// on the GPU (bake once per size, recolour free); SVG/canvas cache their texture
// and re-raster only on size/dirty change.

// ---- per-frame ImageRef ring ----------------------------------------------
// vv_image() stores the ImageRef *pointer* and reads it later at present, so a
// ref handed to the app must stay valid until the frame is presented. We hand
// out refs from a chunked pool (never realloc'd, so live pointers stay valid)
// that begin_frame rewinds. Chunks are added on demand and kept between frames.
#define REF_CHUNK 256
typedef struct RefBlock {
    vv_ImageRef      refs[REF_CHUNK];
    struct RefBlock *next;
} RefBlock;

// ---- icons -----------------------------------------------------------------
typedef struct {
    int      px;   // device-pixel size this mask was baked at
    vv_TexID tex;  // white + coverage(alpha) RGBA mask
} IconBake;

typedef struct {
    cr_svg   *doc;
    IconBake *bakes;
    int       nbake, capbake;
} IconEntry;

// ---- svg docs --------------------------------------------------------------
struct vv_SvgDoc {
    cr_svg     *doc;
    float       w, h;      // intrinsic size (user units)
    int         rw, rh;    // rasterized texture size (device px), 0 = none
    vv_TexID    tex;
    vv_ImageRef ref;       // persistent (points at `tex`), returned by svg_ref
    bool        have;
};

// ---- canvas ----------------------------------------------------------------
struct vv_Canvas {
    vv_Vector  *v;
    cr_context *ctx;
    uint8_t    *buf;       // premultiplied RGBA the app draws into
    int         w, h;
    cr_surface  surf;
    vv_TexID    tex;
    vv_ImageRef ref;
    bool        have;
};

struct vv_Vector {
    vv_Backend *b;
    cr_context *bake;      // scratch for icon/svg rasterization

    RefBlock   *ref_head, *ref_cur;
    int         ref_i;

    IconEntry  *icons;
    int         nicons, capicons;

    uint8_t    *scratch;   // reusable RGBA staging for uploads
    size_t      scratch_cap;
};

// ---- small helpers ---------------------------------------------------------

static void *xgrow(void *p, int *cap, int need, size_t elem) {
    if (need <= *cap) return p;
    int nc = *cap ? *cap * 2 : 8;
    while (nc < need) nc *= 2;
    p = realloc(p, (size_t)nc * elem);
    *cap = nc;
    return p;
}

static uint8_t *scratch_rgba(vv_Vector *v, int w, int h) {
    size_t need = (size_t)w * h * 4;
    if (need > v->scratch_cap) { v->scratch = realloc(v->scratch, need); v->scratch_cap = need; }
    return v->scratch;
}

static vv_ImageRef *ring_next(vv_Vector *v) {
    if (v->ref_i == REF_CHUNK) {
        if (!v->ref_cur->next) v->ref_cur->next = calloc(1, sizeof(RefBlock));
        v->ref_cur = v->ref_cur->next;
        v->ref_i = 0;
    }
    return &v->ref_cur->refs[v->ref_i++];
}

// Rasterize `doc` to fit w x h and upload as a white+coverage mask (the shape's
// alpha becomes the mask; source colours are discarded). Straight alpha, so the
// image shader's `texture * tint` yields correctly-composited tinted coverage.
static vv_TexID bake_icon_mask(vv_Vector *v, cr_svg *doc, int w, int h) {
    uint8_t *px = scratch_rgba(v, w, h);
    cr_rasterize_svg(v->bake, doc, px, w, h, w * 4); // fits + clears, premultiplied
    for (int i = 0; i < w * h; i++) {
        uint8_t a = px[i * 4 + 3];
        px[i * 4 + 0] = 255; px[i * 4 + 1] = 255; px[i * 4 + 2] = 255; px[i * 4 + 3] = a;
    }
    return v->b->texture_create(v->b->ctx, px, w, h, VV_PIXFMT_RGBA8);
}

// Rasterize `doc` to fit w x h and upload full colour. craz outputs premultiplied
// RGBA; the image path blends straight alpha, so un-premultiply before upload.
static vv_TexID bake_svg_color(vv_Vector *v, cr_svg *doc, int w, int h) {
    uint8_t *px = scratch_rgba(v, w, h);
    cr_rasterize_svg(v->bake, doc, px, w, h, w * 4);
    for (int i = 0; i < w * h; i++) {
        uint8_t a = px[i * 4 + 3];
        if (a && a < 255) {
            px[i * 4 + 0] = (uint8_t)(px[i * 4 + 0] * 255 / a);
            px[i * 4 + 1] = (uint8_t)(px[i * 4 + 1] * 255 / a);
            px[i * 4 + 2] = (uint8_t)(px[i * 4 + 2] * 255 / a);
        }
    }
    return v->b->texture_create(v->b->ctx, px, w, h, VV_PIXFMT_RGBA8);
}

// Un-premultiply an app-drawn premultiplied buffer into scratch and upload.
static vv_TexID bake_canvas_tex(vv_Vector *v, const uint8_t *premul, int w, int h) {
    uint8_t *px = scratch_rgba(v, w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t a = premul[i * 4 + 3];
        if (a == 0) { px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 0; px[i * 4 + 3] = 0; continue; }
        px[i * 4 + 0] = a < 255 ? (uint8_t)(premul[i * 4 + 0] * 255 / a) : premul[i * 4 + 0];
        px[i * 4 + 1] = a < 255 ? (uint8_t)(premul[i * 4 + 1] * 255 / a) : premul[i * 4 + 1];
        px[i * 4 + 2] = a < 255 ? (uint8_t)(premul[i * 4 + 2] * 255 / a) : premul[i * 4 + 2];
        px[i * 4 + 3] = a;
    }
    return v->b->texture_create(v->b->ctx, px, w, h, VV_PIXFMT_RGBA8);
}

// ---- lifecycle -------------------------------------------------------------

vv_Vector *vv_vector_new(vv_Backend *backend) {
    if (!backend || !backend->texture_create) return NULL;
    vv_Vector *v = calloc(1, sizeof *v);
    v->b = backend;
    v->bake = cr_context_new();
    v->ref_head = v->ref_cur = calloc(1, sizeof(RefBlock));
    return v;
}

void vv_vector_free(vv_Vector *v) {
    if (!v) return;
    for (int i = 0; i < v->nicons; i++) {
        IconEntry *e = &v->icons[i];
        for (int b = 0; b < e->nbake; b++) v->b->texture_destroy(v->b->ctx, e->bakes[b].tex);
        free(e->bakes);
        if (e->doc) cr_svg_free(e->doc);
    }
    free(v->icons);
    for (RefBlock *rb = v->ref_head; rb;) { RefBlock *n = rb->next; free(rb); rb = n; }
    if (v->bake) cr_context_free(v->bake);
    free(v->scratch);
    free(v);
}

void vv_vector_begin_frame(vv_Vector *v) {
    if (!v) return;
    v->ref_cur = v->ref_head;
    v->ref_i = 0;
}

// ---- icons -----------------------------------------------------------------

static vv_Icon icon_add(vv_Vector *v, cr_svg *doc) {
    if (!doc) return 0;
    v->icons = xgrow(v->icons, &v->capicons, v->nicons + 1, sizeof(IconEntry));
    IconEntry *e = &v->icons[v->nicons++];
    e->doc = doc; e->bakes = NULL; e->nbake = e->capbake = 0;
    return (vv_Icon)v->nicons; // id = index + 1
}

vv_Icon vv_vector_icon_svg(vv_Vector *v, const char *svg_data, int len) {
    if (!v) return 0;
    return icon_add(v, cr_svg_parse(svg_data, (size_t)len));
}

vv_Icon vv_vector_icon_svg_file(vv_Vector *v, const char *path) {
    if (!v) return 0;
    return icon_add(v, cr_svg_parse_file(path));
}

const vv_ImageRef *vv_vector_icon_ref(vv_Vector *v, vv_Icon icon, float px,
                                      float dpi, vv_Color tint) {
    if (!v || icon == 0 || icon > (vv_Icon)v->nicons) return NULL;
    IconEntry *e = &v->icons[icon - 1];
    int dpx = (int)(px * (dpi > 0 ? dpi : 1.0f) + 0.5f);
    if (dpx < 1) dpx = 1;

    vv_TexID tex = 0;
    for (int i = 0; i < e->nbake; i++) if (e->bakes[i].px == dpx) { tex = e->bakes[i].tex; break; }
    if (!tex) {
        tex = bake_icon_mask(v, e->doc, dpx, dpx);
        e->bakes = xgrow(e->bakes, &e->capbake, e->nbake + 1, sizeof(IconBake));
        e->bakes[e->nbake++] = (IconBake){ .px = dpx, .tex = tex };
    }
    vv_ImageRef *r = ring_next(v);
    *r = (vv_ImageRef){ .tex = tex, .uv = { 0, 0, 1, 1 }, .tint = tint };
    return r;
}

uint32_t vv_icon(vv_Ctx *ctx, vv_Vector *v, const char *key, vv_Icon icon,
                 float px, float dpi, vv_Color tint) {
    const vv_ImageRef *r = vv_vector_icon_ref(v, icon, px, dpi, tint);
    if (!r) return 0;
    return vv_image(ctx, key, r, vv_fixed(px), vv_fixed(px));
}

// ---- svg -------------------------------------------------------------------

static vv_SvgDoc *svg_wrap(cr_svg *doc) {
    if (!doc) return NULL;
    vv_SvgDoc *d = calloc(1, sizeof *d);
    d->doc = doc;
    d->w = cr_svg_width(doc);
    d->h = cr_svg_height(doc);
    return d;
}

vv_SvgDoc *vv_vector_svg(vv_Vector *v, const char *data, int len) {
    (void)v; return svg_wrap(cr_svg_parse(data, (size_t)len));
}
vv_SvgDoc *vv_vector_svg_file(vv_Vector *v, const char *path) {
    (void)v; return svg_wrap(cr_svg_parse_file(path));
}

void vv_vector_svg_free(vv_Vector *v, vv_SvgDoc *doc) {
    if (!doc) return;
    if (doc->have && v) v->b->texture_destroy(v->b->ctx, doc->tex);
    if (doc->doc) cr_svg_free(doc->doc);
    free(doc);
}

void vv_vector_svg_size(const vv_SvgDoc *doc, float *w, float *h) {
    if (!doc) return;
    if (w) *w = doc->w;
    if (h) *h = doc->h;
}

const vv_ImageRef *vv_vector_svg_ref(vv_Vector *v, vv_SvgDoc *doc, int w, int h) {
    if (!v || !doc || w < 1 || h < 1) return NULL;
    if (!doc->have || doc->rw != w || doc->rh != h) {
        if (doc->have) v->b->texture_destroy(v->b->ctx, doc->tex);
        doc->tex = bake_svg_color(v, doc->doc, w, h);
        doc->rw = w; doc->rh = h; doc->have = true;
        doc->ref = (vv_ImageRef){ .tex = doc->tex, .uv = { 0, 0, 1, 1 }, .tint = { 1, 1, 1, 1 } };
    }
    // Hand back through the frame ring so the pointer is uniform with icon_ref.
    vv_ImageRef *r = ring_next(v);
    *r = doc->ref;
    return r;
}

// ---- canvas ----------------------------------------------------------------

vv_Canvas *vv_vector_canvas(vv_Vector *v, int w, int h) {
    if (!v) return NULL;
    vv_Canvas *cv = calloc(1, sizeof *cv);
    cv->v = v;
    cv->ctx = cr_context_new();
    vv_canvas_resize(cv, w, h);
    return cv;
}

void vv_canvas_free(vv_Canvas *cv) {
    if (!cv) return;
    if (cv->have) cv->v->b->texture_destroy(cv->v->b->ctx, cv->tex);
    if (cv->ctx) cr_context_free(cv->ctx);
    free(cv->buf);
    free(cv);
}

void vv_canvas_resize(vv_Canvas *cv, int w, int h) {
    if (!cv || w < 1 || h < 1) return;
    if (cv->w == w && cv->h == h && cv->buf) return;
    free(cv->buf);
    cv->w = w; cv->h = h;
    cv->buf = malloc((size_t)w * h * 4);
    cv->surf = (cr_surface){ cv->buf, w, h, w * 4, CR_FORMAT_RGBA8_PREMUL };
    cv->have = false; // texture stale until next commit
}

void vv_canvas_begin(vv_Canvas *cv, cr_context **ctx, cr_surface **surf) {
    if (!cv) return;
    memset(cv->buf, 0, (size_t)cv->w * cv->h * 4); // transparent
    if (ctx)  *ctx  = cv->ctx;
    if (surf) *surf = &cv->surf;
}

void vv_canvas_commit(vv_Canvas *cv) {
    if (!cv) return;
    if (cv->have) cv->v->b->texture_destroy(cv->v->b->ctx, cv->tex);
    cv->tex = bake_canvas_tex(cv->v, cv->buf, cv->w, cv->h);
    cv->have = true;
    cv->ref = (vv_ImageRef){ .tex = cv->tex, .uv = { 0, 0, 1, 1 }, .tint = { 1, 1, 1, 1 } };
}

const vv_ImageRef *vv_canvas_ref(const vv_Canvas *cv) {
    return cv && cv->have ? &cv->ref : NULL;
}
