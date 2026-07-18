// vv_context.h — the frame lifecycle and immediate-mode build API (§4).
//
// User code declares the TARGET for the current logical state; the retained
// tree holds the ACTUAL value. begin_frame/build/end_frame is the whole loop.
#ifndef VV_CONTEXT_H
#define VV_CONTEXT_H

#include "vv_arena.h"
#include "vv_command.h"
#include "vv_id.h"
#include "vv_node.h"

typedef struct {
    vv_Vec2 mouse;
    bool    mouse_down;
    float   wheel;
    // Phase 4 fills this out (keys, text, modifiers).
} vv_Input;

// Frame tier actually executed (§4.2). Reported for diagnostics/tests.
typedef enum { VV_TIER_IDLE, VV_TIER_PRESENT, VV_TIER_BUILD } vv_FrameTier;

#define VV_BUILD_STACK_MAX 256

typedef struct vv_Ctx {
    vv_Arena    persistent;
    vv_Arena    frame;
    vv_NodePool pool;

    uint64_t frame_index;
    float    dt;
    vv_Input input;
    float    win_w, win_h, dpi_scale;
    float    animation_scale;   // §18 kill switch; 1.0 normal, 0.0 = snap

    bool     idle_mode;         // §4.2 opt-in
    bool     tree_dirty;        // forces a Build tier next frame
    uint32_t unsettled_springs; // Present-tier gate

    uint32_t root;              // pool index of the root node

    // Build-time stack.
    uint32_t stack[VV_BUILD_STACK_MAX];
    uint32_t seq_counter[VV_BUILD_STACK_MAX];
    int      depth;
    bool     in_build;

    vv_CommandBuffer cmds;

    vv_FrameTier last_tier;
} vv_Ctx;

void vv_init(vv_Ctx *ctx);
void vv_shutdown(vv_Ctx *ctx);

void vv_set_window(vv_Ctx *ctx, float w, float h, float dpi_scale);
void vv_set_idle_mode(vv_Ctx *ctx, bool on);
void vv_set_animation_scale(vv_Ctx *ctx, float scale);
void vv_invalidate(vv_Ctx *ctx);

void vv_begin_frame(vv_Ctx *ctx, float dt, const vv_Input *input);
vv_CommandBuffer *vv_end_frame(vv_Ctx *ctx);

// ---- tree building -------------------------------------------------------

// Open a container. Returns its pool index (opaque handle for queries).
// `key`/`klen` may be NULL/0 to rely on sequence identity (§3.1).
uint32_t vv_box_keyed(vv_Ctx *ctx, const char *key, size_t klen,
                      vv_LayoutDecl decl, vv_Style style);
void     vv_end_box(vv_Ctx *ctx);

// Leaf text node.
uint32_t vv_text_keyed(vv_Ctx *ctx, const char *key, size_t klen,
                       const char *utf8, vv_Style style);

// Convenience wrappers with no explicit key.
static inline uint32_t vv_box(vv_Ctx *ctx, vv_LayoutDecl d, vv_Style s) {
    return vv_box_keyed(ctx, NULL, 0, d, s);
}
static inline uint32_t vv_text(vv_Ctx *ctx, const char *utf8, vv_Style s) {
    return vv_text_keyed(ctx, NULL, 0, utf8, s);
}

// Scoped container: `VV_BOX(ctx, decl, style) { ...children... }`.
// The loop variable is line-unique so nested VV_BOX blocks don't shadow.
#define VV_CAT_(a, b) a##b
#define VV_CAT(a, b)  VV_CAT_(a, b)
#define VV_BOX(ctx, decl, style)                                            \
    for (int VV_CAT(_vv_once_, __LINE__) = (vv_box((ctx), (decl), (style)), 1); \
         VV_CAT(_vv_once_, __LINE__);                                       \
         VV_CAT(_vv_once_, __LINE__) = (vv_end_box(ctx), 0))

// Access a node by handle (valid only within the current frame).
static inline vv_Node *vv_node(vv_Ctx *ctx, uint32_t index) {
    return vv_pool_get(&ctx->pool, index);
}

#endif // VV_CONTEXT_H
