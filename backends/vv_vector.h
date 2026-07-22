// vv_vector.h — craz-backed vector services for a Verve backend: crisp icons,
// full-colour SVG, and an app-driven dynamic canvas. All three rasterize on the
// CPU with craz (../craz) and feed the core's existing image pipeline — each
// produces a vv_ImageRef you hand to vv_image()/vv_icon(), so no new command
// types or shaders are needed.
//
// The module is backend-agnostic: it uploads textures through a vv_Backend
// vtable (texture_create/destroy), so any backend that implements those can use
// it. The SDL/GL backend wires a lazily-created instance onto the app and
// exposes vv_app_* wrappers (see vv_sdl_gl.h) for the turn-key path.
//
// Efficiency model (this is the point of the module):
//   • Icons bake ONE A8-coverage mask per (icon, device-pixel size). Colour is
//     multiplied on the GPU via the ImageRef tint, so hover/disabled/theme
//     recolour never re-bakes — the same mask serves every colour.
//   • SVG documents cache their rasterized texture and re-raster only when the
//     requested pixel size changes.
//   • The canvas re-rasters only when the app marks it dirty, and can reuse a
//     craz retained scene for animated redraws.
#ifndef VV_VECTOR_H
#define VV_VECTOR_H

#include "verve/verve.h"
#include "craz/craz.h" // cr_context / cr_surface exposed by the canvas API

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vv_Vector vv_Vector;

// Create/destroy. `backend` must outlive the returned instance (textures are
// created and destroyed through it).
vv_Vector *vv_vector_new(vv_Backend *backend);
void       vv_vector_free(vv_Vector *v);

// Reset the per-frame ImageRef ring (see the note on icon_ref/svg_ref lifetime
// below). Call once at the top of each frame. Optional — the ring is large and
// wraps — but calling it bounds churn and avoids overwrite for exiting nodes.
void       vv_vector_begin_frame(vv_Vector *v);

// ---- Icons: single-colour coverage, tinted on the GPU -----------------------
// An icon is a monochrome mask derived from an SVG's shape coverage (its own
// colours are discarded — use the SVG API below for full colour). Bake once,
// recolour for free.
typedef uint32_t vv_Icon; // 0 = invalid

vv_Icon vv_vector_icon_svg(vv_Vector *v, const char *svg_data, int len);
vv_Icon vv_vector_icon_svg_file(vv_Vector *v, const char *path);

// A ready-to-draw image ref for `icon` at `px` logical size on a `dpi` display,
// tinted `tint`. The coverage mask is baked once per (icon, round(px*dpi)); the
// tint is applied by the shader, so a different colour costs nothing.
//
// LIFETIME: the returned pointer is valid until vv_vector_begin_frame() is next
// called (it lives in a per-frame ring). That is exactly long enough to pass to
// vv_image()/vv_icon() this frame — do not store it across frames.
const vv_ImageRef *vv_vector_icon_ref(vv_Vector *v, vv_Icon icon, float px,
                                      float dpi, vv_Color tint);

// Sugar: build an icon leaf sized `px` x `px`. Returns the node id (0 if the
// icon is invalid). Equivalent to vv_image() with an icon ref.
uint32_t vv_icon(vv_Ctx *ctx, vv_Vector *v, const char *key, vv_Icon icon,
                 float px, float dpi, vv_Color tint);

// A clickable icon button: a padded, rounded hit target around a tinted icon,
// with theme-driven hover/press fills. Returns true the frame it is clicked
// (poll it like vv_tree_item). GHOST is a bare glyph that lights up on hover
// (toolbars, secondary actions); SOLID paints the brand fill as a pill (a
// primary action, e.g. a play button). `tint` colours the glyph; pass a
// zero-alpha colour ({0}) to take a sensible default for the style.
typedef enum { VV_ICON_BTN_GHOST, VV_ICON_BTN_SOLID } vv_IconBtnStyle;
bool vv_icon_button(vv_Ctx *ctx, vv_Vector *v, const char *key, vv_Icon icon,
                    float px, float dpi, vv_Color tint, vv_IconBtnStyle style);

// ---- SVG: full-colour documents rasterized to a texture ---------------------
typedef struct vv_SvgDoc vv_SvgDoc;

vv_SvgDoc *vv_vector_svg(vv_Vector *v, const char *data, int len);
vv_SvgDoc *vv_vector_svg_file(vv_Vector *v, const char *path);
void       vv_vector_svg_free(vv_Vector *v, vv_SvgDoc *doc);
void       vv_vector_svg_size(const vv_SvgDoc *doc, float *w, float *h); // user units

// Raster `doc` to fit `w`x`h` device pixels (preserving aspect, centered) and
// return a ref. Cached — re-rasters only when (w,h) changes. Same per-frame ring
// lifetime as icon_ref.
const vv_ImageRef *vv_vector_svg_ref(vv_Vector *v, vv_SvgDoc *doc, int w, int h);

// ---- Canvas: app-driven craz drawing into a texture -------------------------
// The escape hatch for dynamic/custom vector content (plots, editors, generated
// art). The app draws with the full craz API into an owned surface, then commits
// it to a texture. Re-raster only when your content changes; between changes the
// cached texture is reused for free.
//
//   vv_Canvas *cv = vv_vector_canvas(v, w, h);
//   if (dirty) {
//       cr_context *ctx; cr_surface *s;
//       vv_canvas_begin(cv, &ctx, &s);      // clears to transparent
//       cr_fill_path(ctx, s, NULL, path, &m, &paint, CR_FILL_NONZERO);
//       vv_canvas_commit(cv);               // un-premultiplies + uploads
//   }
//   vv_image(c, "art", vv_canvas_ref(cv), vv_grow(1), vv_grow(1));
typedef struct vv_Canvas vv_Canvas;

vv_Canvas *vv_vector_canvas(vv_Vector *v, int w, int h);
void       vv_canvas_free(vv_Canvas *cv);
// Resize the backing surface (no-op if unchanged). Invalidates the texture.
void       vv_canvas_resize(vv_Canvas *cv, int w, int h);
// Begin a redraw: clears the surface to transparent and hands back the craz
// context + surface to draw into. Either out-param may be NULL.
void       vv_canvas_begin(vv_Canvas *cv, cr_context **ctx, cr_surface **surf);
// Upload the current surface (un-premultiplied) to the canvas texture.
void       vv_canvas_commit(vv_Canvas *cv);
// A ref to the last committed texture (persistent for the life of the canvas).
const vv_ImageRef *vv_canvas_ref(const vv_Canvas *cv);

#ifdef __cplusplus
}
#endif

#endif // VV_VECTOR_H
