// vv_draw.h — a tiny backend-agnostic canvas for custom widgets (§14.5).
//
// Freeform vector content (plot lines, an editable curve, a crosshair) can't be
// expressed as boxes, and the core has no GPU. Instead a widget calls the
// vv_draw_* builders during build to attach a *draw-list* to its node — polylines
// and filled polygons in the node's LOCAL coordinate space (origin at the node's
// top-left). Present lowers each into a VV_CMD_POLY, translating to window space
// by the node's animated rect, so the geometry pans/springs with layout for free.
//
// The geometry is copied into the frame arena, so the caller's point arrays need
// not outlive the call. This is the vector analogue of the custom-draw leaf
// (vv_custom, §14.3), but stays inside the reconciled/animated tree.
#ifndef VV_DRAW_H
#define VV_DRAW_H

#include "vv_command.h" // vv_CmdPoly flags, vv_Vec2/vv_Color via vv_types.h

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vv_Ctx vv_Ctx;

// One polyline/polygon in a node's draw-list. Points are LOCAL to the node.
typedef struct vv_Poly {
    vv_Vec2        *pts;
    uint32_t        count;
    float           width;  // stroke width (logical px); ignored when filled
    vv_Color        color;
    uint8_t         flags;  // VV_POLY_CLOSED | VV_POLY_FILL
    struct vv_Poly *next;
} vv_Poly;

typedef struct vv_DrawList {
    vv_Poly *head, *tail;
} vv_DrawList;

// Builders — call during build with a node handle (from vv_box_keyed / vv_custom
// / any widget). Coordinates are local to that node's rect. Colours may carry
// alpha; present multiplies in the node's inherited opacity.
void vv_draw_line(vv_Ctx *ctx, uint32_t node, vv_Vec2 a, vv_Vec2 b,
                  float width, vv_Color color);
void vv_draw_polyline(vv_Ctx *ctx, uint32_t node, const vv_Vec2 *pts, int n,
                      float width, vv_Color color);
void vv_draw_polygon(vv_Ctx *ctx, uint32_t node, const vv_Vec2 *pts, int n,
                     vv_Color fill);
// n filled dots of radius `r` (approximated by the backend as small discs).
void vv_draw_points(vv_Ctx *ctx, uint32_t node, const vv_Vec2 *pts, int n,
                    float r, vv_Color color);

#ifdef __cplusplus
}
#endif

#endif // VV_DRAW_H
