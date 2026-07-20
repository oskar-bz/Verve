#include "verve/vv_backend.h"

// Walk the command buffer and dispatch to the backend, coalescing runs of
// same-kind draw commands so the backend gets batched arrays (§9.2). Scissor,
// transform and custom commands break the current run and are dispatched
// individually.

void vv_render(const vv_Backend *b, const vv_CommandBuffer *cmds,
               int w, int h, float dpi_scale) {
    if (b->begin) b->begin(b->ctx, w, h, dpi_scale);

    uint32_t i = 0;
    while (i < cmds->count) {
        vv_CmdKind kind = cmds->items[i].kind;

        // Coalesce a run of RECT/TEXT/IMAGE/POLY into a temp array on the stack.
        if (kind == VV_CMD_RECT || kind == VV_CMD_TEXT || kind == VV_CMD_IMAGE ||
            kind == VV_CMD_POLY) {
            uint32_t j = i;
            while (j < cmds->count && cmds->items[j].kind == kind) j++;
            int n = (int)(j - i);

            // The union layout means we can't hand the backend a strided view
            // without copying; do a small bounded copy per run.
            if (kind == VV_CMD_RECT && b->draw_rects) {
                for (uint32_t k = i; k < j; ) {
                    vv_CmdRect tmp[64]; int m = 0;
                    while (k < j && m < 64) tmp[m++] = cmds->items[k++].as.rect;
                    b->draw_rects(b->ctx, tmp, m);
                }
            } else if (kind == VV_CMD_TEXT && b->draw_text) {
                for (uint32_t k = i; k < j; ) {
                    vv_CmdText tmp[64]; int m = 0;
                    while (k < j && m < 64) tmp[m++] = cmds->items[k++].as.text;
                    b->draw_text(b->ctx, tmp, m);
                }
            } else if (kind == VV_CMD_IMAGE && b->draw_image) {
                for (uint32_t k = i; k < j; ) {
                    vv_CmdImage tmp[64]; int m = 0;
                    while (k < j && m < 64) tmp[m++] = cmds->items[k++].as.image;
                    b->draw_image(b->ctx, tmp, m);
                }
            } else if (kind == VV_CMD_POLY && b->draw_polys) {
                for (uint32_t k = i; k < j; ) {
                    vv_CmdPoly tmp[64]; int m = 0;
                    while (k < j && m < 64) tmp[m++] = cmds->items[k++].as.poly;
                    b->draw_polys(b->ctx, tmp, m);
                }
            }
            (void)n;
            i = j;
            continue;
        }

        const vv_Command *c = &cmds->items[i++];
        switch (c->kind) {
            case VV_CMD_SCISSOR_PUSH:   if (b->push_scissor)   b->push_scissor(b->ctx, c->as.scissor); break;
            case VV_CMD_SCISSOR_POP:    if (b->pop_scissor)    b->pop_scissor(b->ctx); break;
            case VV_CMD_TRANSFORM_PUSH: if (b->push_transform) b->push_transform(b->ctx, c->as.xform); break;
            case VV_CMD_TRANSFORM_POP:  if (b->pop_transform)  b->pop_transform(b->ctx); break;
            case VV_CMD_CUSTOM:         if (b->custom) b->custom(b->ctx, c->as.custom.id, c->as.custom.payload, c->as.custom.rect); break;
            default: break;
        }
    }

    if (b->end) b->end(b->ctx);
}
