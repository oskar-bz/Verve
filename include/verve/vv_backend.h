// vv_backend.h — the backend interface (§15).
//
// Fifteen function pointers. The core produces a command buffer; a backend
// consumes it. `measure_text` is the one leak: it's called during layout, not
// rendering (§8.1). A second backend should take a weekend.
#ifndef VV_BACKEND_H
#define VV_BACKEND_H

#include "vv_command.h"
#include "vv_types.h"

typedef enum {
    VV_CURSOR_DEFAULT,
    VV_CURSOR_POINTER,
    VV_CURSOR_TEXT,
    VV_CURSOR_RESIZE_H,
    VV_CURSOR_RESIZE_V,
} vv_CursorShape;

typedef enum { VV_PIXFMT_RGBA8, VV_PIXFMT_A8 } vv_PixFmt;

typedef struct vv_Backend {
    void *ctx;

    void (*begin)(void *ctx, int w, int h, float dpi_scale);
    void (*end)(void *ctx);

    void (*draw_rects)(void *ctx, const vv_CmdRect *rects, int n);
    void (*draw_text)(void *ctx, const vv_CmdText *runs, int n);
    void (*draw_image)(void *ctx, const vv_CmdImage *imgs, int n);

    void (*push_scissor)(void *ctx, vv_Rect r);
    void (*pop_scissor)(void *ctx);
    void (*push_transform)(void *ctx, vv_Mat23 m);
    void (*pop_transform)(void *ctx);

    void (*custom)(void *ctx, uint32_t id, void *payload, vv_Rect r);

    vv_TexID  (*texture_create)(void *ctx, const void *px, int w, int h, vv_PixFmt);
    void      (*texture_destroy)(void *ctx, vv_TexID);
    vv_FontID (*font_load)(void *ctx, const void *ttf, size_t len);

    vv_Vec2 (*measure_text)(void *ctx, const char *s, int len,
                            vv_FontID, float size, float wrap_width);

    const char *(*clipboard_get)(void *ctx);
    void        (*clipboard_set)(void *ctx, const char *s);
    void        (*set_cursor)(void *ctx, vv_CursorShape);
} vv_Backend;

// Dispatch a command buffer to a backend, batching adjacent same-kind commands
// into the run-based draw_* calls (§9.2). Defined in vv_backend.c.
void vv_render(const vv_Backend *b, const vv_CommandBuffer *cmds,
               int w, int h, float dpi_scale);

#endif // VV_BACKEND_H
