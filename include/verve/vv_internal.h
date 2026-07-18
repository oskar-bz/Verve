// vv_internal.h — cross-module pipeline entry points not part of the public
// API. These are the seams between Build and Present (§4.1); each is a whole
// module that later phases flesh out.
#ifndef VV_INTERNAL_H
#define VV_INTERNAL_H

#include "vv_node.h"

struct vv_Ctx;

// Build phase: assign every node a layout_rect (§5).
void vv_layout_run(vv_NodePool *p, uint32_t root, vv_Rect window);

// Present phase (§4.1 steps 6-8): tick springs toward targets, compute
// actual_rect/actual style, and emit the command buffer. No user code, no
// reconciliation, no layout — this is what makes animation cheap.
void vv_present(struct vv_Ctx *ctx);

#endif // VV_INTERNAL_H
