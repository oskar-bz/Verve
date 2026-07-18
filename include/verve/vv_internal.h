// vv_internal.h — cross-module pipeline entry points not part of the public
// API. These are the seams between Build and Present (§4.1); each is a whole
// module that later phases flesh out.
#ifndef VV_INTERNAL_H
#define VV_INTERNAL_H

#include "vv_node.h"

struct vv_Ctx;

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
