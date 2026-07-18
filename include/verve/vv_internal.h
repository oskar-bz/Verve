// vv_internal.h — cross-module pipeline entry points not part of the public
// API. These are the seams between Build and Present (§4.1); each is a whole
// module that later phases flesh out.
#ifndef VV_INTERNAL_H
#define VV_INTERNAL_H

#include "vv_node.h"

// Build phase: assign every node a layout_rect (§5). Phase 0 placeholder.
void vv_layout_run(vv_NodePool *p, uint32_t root, vv_Rect window);

#endif // VV_INTERNAL_H
