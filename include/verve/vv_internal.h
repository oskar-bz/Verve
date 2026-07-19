// vv_internal.h — cross-module pipeline entry points not part of the public
// API. These are the seams between Build and Present (§4.1); each is a whole
// module that later phases flesh out.
#ifndef VV_INTERNAL_H
#define VV_INTERNAL_H

#include "vv_node.h"

struct vv_Ctx;

// Vertical scrollbar thumb geometry for a scroll container, shared by input
// (hit-testing the thumb for dragging) and present (drawing it) so the two
// never disagree. Returns false when the y axis doesn't overflow.
static inline bool vv_scrollbar_thumb_v(const vv_Node *n, vv_Rect *out) {
    const float W = 6.0f, M = 2.0f, MIN = 28.0f;
    if (!n->decl.scroll_y || n->scroll_max_y <= 0.5f || n->actual_rect.h <= 0.0f)
        return false;
    vv_Rect v = n->actual_rect;
    float content = v.h + n->scroll_max_y;
    float len = vv_clampf(v.h * v.h / content, MIN, v.h);
    float frac = vv_clampf(n->scroll_y.x / n->scroll_max_y, 0.0f, 1.0f);
    *out = vv_rect(v.x + v.w - W - M, v.y + frac * (v.h - len), W, len);
    return true;
}

// Build phase: assign every node a layout_rect (§5). Reads pool/root/window and
// the text measure callback off the context.
void vv_layout_run(struct vv_Ctx *ctx);

// Present phase (§4.1 steps 6-8): tick springs toward targets, compute
// actual_rect/actual style, and emit the command buffer. No user code, no
// reconciliation, no layout — this is what makes animation cheap.
void vv_present(struct vv_Ctx *ctx);

// Input phase (§11): hit-test the pointer against last frame's actual_rect,
// resolve hover/active/focus/capture transitions, apply scroll. Runs at
// begin_frame so build code queries fresh input against seen geometry (§4.5).
void vv_input_process(struct vv_Ctx *ctx);

#endif // VV_INTERNAL_H
