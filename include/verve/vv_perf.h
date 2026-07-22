// vv_perf.h — highly granular, zero-cost performance instrumentation (§X).
//
// Enable with -DVV_PERF at compile time. When disabled, all macros expand to
// nothing — zero runtime overhead, zero memory footprint.
//
// Granularity: every pipeline phase and its major sub-phases are timed
// separately so you can see exactly where budget goes:
//
//   frame total
//   ├─ input          hit-test + interaction resolution
//   ├─ build_begin    arena reset + root setup
//   ├─ reconcile      pool sweep (EXITING node cleanup)
//   ├─ layout         4-pass constraint solver (each pass separated)
//   └─ present        style tick, FLIP rect, scroll, command emission
//
// Usage:
//   vv_Ctx ctx; vv_init(&ctx); vv_perf_init(&ctx);
//   while (...) {
//       vv_run_frame(&ctx, dt, &input, update, view, state);
//   }
//   vv_perf_print(&ctx);
//   vv_perf_reset(&ctx);  // between test runs

#ifndef VV_PERF_H
#define VV_PERF_H

#include "verve/vv_types.h"

// Forward declaration — vv_Ctx is defined in vv_context.h which includes us.
typedef struct vv_Ctx vv_Ctx;

// ---------------------------------------------------------------------------
// Phase ID enum — one entry per measurable unit.
// ---------------------------------------------------------------------------

typedef enum {
    VV_PERF_FRAME_TOTAL,
    VV_PERF_INPUT,
    VV_PERF_BUILD_BEGIN,
    VV_PERF_RECONCILE,
    VV_PERF_LAYOUT,
    VV_PERF_LAYOUT_P1,    // bottom-up intrinsic widths
    VV_PERF_LAYOUT_P2,    // top-down width distribution
    VV_PERF_LAYOUT_P3,    // bottom-up intrinsic heights
    VV_PERF_LAYOUT_P4,    // top-down height distribution + positioning
    VV_PERF_PRESENT,
    VV_PERF_PRESENT_STYLE,
    VV_PERF_PRESENT_RECT,
    VV_PERF_PRESENT_SCROLL,
    VV_PERF_PRESENT_EMIT,
    VV_PERF_PRESENT_EXITING,
    VV_PERF_COUNT,
} vv_PerfPhaseID;

// ---------------------------------------------------------------------------
// Accumulation sample — one per phase/sub-phase.
// ---------------------------------------------------------------------------

typedef struct {
    uint64_t ns;
    uint64_t frames;
    uint64_t max_ns;
} vv_PerfSample;

// ---------------------------------------------------------------------------
// Timing context — embedded in vv_Ctx.
// ---------------------------------------------------------------------------

typedef struct vv_PerfCtx {
    vv_PerfSample samples[VV_PERF_COUNT];
    uint64_t tier_build;
    uint64_t tier_present;
    uint64_t tier_idle;
    uint64_t frame_start_ns;
} vv_PerfCtx;

// ---------------------------------------------------------------------------
// Zero-cost macros — expand to nothing when VV_PERF is disabled.
// ---------------------------------------------------------------------------

#ifdef VV_PERF

void vv_perf_init(vv_Ctx *ctx);
void vv_perf_print(const vv_Ctx *ctx);
void vv_perf_reset(vv_Ctx *ctx);
void vv_perf_start(vv_Ctx *ctx, vv_PerfPhaseID phase);
void vv_perf_end(vv_Ctx *ctx, vv_PerfPhaseID phase);
void vv_perf_count(vv_Ctx *ctx, vv_PerfPhaseID phase);

#define VV_PERF_INIT(ctx)      vv_perf_init((ctx))
#define VV_PERF_PRINT(ctx)     vv_perf_print((ctx))
#define VV_PERF_RESET(ctx)     vv_perf_reset((ctx))
#define VV_PERF_START(ctx, id) vv_perf_start((ctx), (vv_PerfPhaseID)(id))
#define VV_PERF_END(ctx, id)   vv_perf_end((ctx), (vv_PerfPhaseID)(id))
#define VV_PERF_COUNT(ctx, id) vv_perf_count((ctx), (vv_PerfPhaseID)(id))

#else

#define VV_PERF_INIT(ctx)      do {} while(0)
#define VV_PERF_PRINT(ctx)     do {} while(0)
#define VV_PERF_RESET(ctx)     do {} while(0)
#define VV_PERF_START(ctx, id) do {} while(0)
#define VV_PERF_END(ctx, id)   do {} while(0)
#define VV_PERF_COUNT(ctx, id) do {} while(0)

#endif

#endif // VV_PERF_H
