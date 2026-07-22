// vv_perf.c — granular performance instrumentation for the Verve pipeline (§X).
//
// When compiled with -DVV_PERF, every major pipeline phase and sub-phase is
// timed via monotonic clock and accumulated in vv_Ctx.perf. When disabled, the
// entire file is a no-op — zero runtime overhead, zero memory footprint.

#define _POSIX_C_SOURCE 199309L
#include "verve/vv_context.h"
#include "verve/vv_perf.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Monotonic clock — CLOCK_MONOTONIC (never goes backward).
// ---------------------------------------------------------------------------

static inline uint64_t perf_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// ---------------------------------------------------------------------------
// Phase names — used only when printing.
// ---------------------------------------------------------------------------

static const char *phase_name(vv_PerfPhaseID id) {
    switch (id) {
        case VV_PERF_FRAME_TOTAL:    return "frame";
        case VV_PERF_INPUT:          return "input";
        case VV_PERF_BUILD_BEGIN:    return "build_begin";
        case VV_PERF_RECONCILE:      return "reconcile";
        case VV_PERF_LAYOUT:         return "layout";
        case VV_PERF_LAYOUT_P1:      return "  ├─ pass1_width (intrinsic)";
        case VV_PERF_LAYOUT_P2:      return "  ├─ pass2_width (distribute)";
        case VV_PERF_LAYOUT_P3:      return "  ├─ pass3_height (intrinsic)";
        case VV_PERF_LAYOUT_P4:      return "  ├─ pass4_height (position)";
        case VV_PERF_PRESENT:        return "present";
        case VV_PERF_PRESENT_STYLE:  return "  ├─ style_tick";
        case VV_PERF_PRESENT_RECT:   return "  ├─ rect_animate (FLIP)";
        case VV_PERF_PRESENT_SCROLL: return "  ├─ scroll_step";
        case VV_PERF_PRESENT_EMIT:   return "  ├─ emit";
        case VV_PERF_PRESENT_EXITING:return "  └─ exiting";
        default:                     return "???";
    }
}

static bool is_child_phase(vv_PerfPhaseID id) {
    return (id >= VV_PERF_LAYOUT_P1 && id <= VV_PERF_LAYOUT_P4) ||
           (id >= VV_PERF_PRESENT_STYLE && id <= VV_PERF_PRESENT_EXITING);
}

// ---------------------------------------------------------------------------
// Accumulate a sample — called by vv_perf_start (single-call design).
// ---------------------------------------------------------------------------

static void accumulate(vv_Ctx *ctx, vv_PerfPhaseID id, uint64_t delta_ns) {
    assert((int)id < VV_PERF_COUNT);
    vv_PerfSample *s = &ctx->perf.perf.samples[id];
    s->ns    += delta_ns;
    s->frames++;
    if (delta_ns > s->max_ns) s->max_ns = delta_ns;
}

// ---------------------------------------------------------------------------
// Public API — single-call design: vv_perf_start does all timing.
// vv_perf_end is a no-op (included for API symmetry with potential future use).
// ---------------------------------------------------------------------------

void vv_perf_init(vv_Ctx *ctx) {
    memset(&ctx->perf, 0, sizeof(ctx->perf));
    ctx->perf.active = true;
    ctx->perf.perf.frame_start_ns = perf_now_ns();
}

void vv_perf_print(const vv_Ctx *ctx) {
    const vv_PerfCtx *p = &ctx->perf.perf;

    uint64_t total_ns = p->samples[VV_PERF_FRAME_TOTAL].ns;
    uint64_t total_frames = p->samples[VV_PERF_FRAME_TOTAL].frames;

    if (total_frames == 0) {
        fprintf(stderr, "(no frames sampled)\n");
        return;
    }

    double avg_ms = (double)total_ns / (double)total_frames / 1000000.0;
    double fps = 1000.0 / avg_ms;

    fprintf(stderr, "\n");
    fprintf(stderr, "═══════════════════════════════════════════════════════════════\n");
    fprintf(stderr, "  Verve Performance Report  (%" PRIu64 " frames)\n", total_frames);
    fprintf(stderr, "═══════════════════════════════════════════════════════════════\n");
    fprintf(stderr, "  Avg: %8.3f ms   |   FPS: %8.1f   |   Tier: BUILD=%" PRIu64 " PRESENT=%" PRIu64 " IDLE=%" PRIu64 "\n",
            avg_ms, fps,
            p->tier_build, p->tier_present, p->tier_idle);
    fprintf(stderr, "═══════════════════════════════════════════════════════════════\n");
    fprintf(stderr, "\n");

    // Print main phases first (top-level, not children).
    for (int i = 0; i < VV_PERF_COUNT; i++) {
        vv_PerfPhaseID id = (vv_PerfPhaseID)i;
        if (is_child_phase(id)) continue;

        const vv_PerfSample *s = &p->samples[id];
        if (s->frames == 0) continue;

        double ms = (double)s->ns / (double)s->frames / 1000000.0;
        double pct = total_ns > 0 ? (100.0 * (double)s->ns / (double)total_ns) : 0.0;
        double max_ms = (double)s->max_ns / 1000000.0;

        int bars = (int)(pct / 2.0);
        if (bars > 30) bars = 30;

        fprintf(stderr, "  %-28s %8.3f ms  %5.1f%%", phase_name(id), ms, pct);
        for (int b = 0; b < bars; b++) fprintf(stderr, "█");
        fprintf(stderr, "   peak: %6.2f ms\n", max_ms);
    }

    fprintf(stderr, "\n");

    // Print child phases indented.
    bool printed_header = false;
    for (int i = 0; i < VV_PERF_COUNT; i++) {
        vv_PerfPhaseID id = (vv_PerfPhaseID)i;
        if (!is_child_phase(id)) continue;

        const vv_PerfSample *s = &p->samples[id];
        if (s->frames == 0) continue;

        double ms = (double)s->ns / (double)s->frames / 1000000.0;
        double max_ms = (double)s->max_ns / 1000000.0;

        if (!printed_header) {
            fprintf(stderr, "  ┌─ Sub-phase breakdown\n");
            fprintf(stderr, "  │\n");
            printed_header = true;
        }

        fprintf(stderr, "  │  %-28s %8.3f ms", phase_name(id), ms);
        if (s->max_ns > 0) fprintf(stderr, "   peak: %6.2f ms", max_ms);
        fprintf(stderr, "\n");
    }

    if (printed_header) fprintf(stderr, "  │\n");

    fprintf(stderr, "═══════════════════════════════════════════════════════════════\n");
    fprintf(stderr, "\n");
}

void vv_perf_reset(vv_Ctx *ctx) {
    memset(ctx->perf.perf.samples, 0, sizeof(ctx->perf.perf.samples));
    ctx->perf.perf.tier_build = 0;
    ctx->perf.perf.tier_present = 0;
    ctx->perf.perf.tier_idle = 0;
    ctx->perf.perf.frame_start_ns = 0;
}

void vv_perf_start(vv_Ctx *ctx, vv_PerfPhaseID id) {
    uint64_t end = perf_now_ns();
    uint64_t start = ctx->perf.perf.frame_start_ns;
    if (start == 0) start = end;

    uint64_t delta = (end >= start) ? (end - start) : 0;
    accumulate(ctx, id, delta);

    // Accumulate into parent phase.
    if (is_child_phase(id)) {
        if (id >= VV_PERF_LAYOUT_P1 && id <= VV_PERF_LAYOUT_P4)
            accumulate(ctx, VV_PERF_LAYOUT, delta);
        else if (id >= VV_PERF_PRESENT_STYLE && id <= VV_PERF_PRESENT_EXITING)
            accumulate(ctx, VV_PERF_PRESENT, delta);
    }

    ctx->perf.perf.frame_start_ns = end;
}

void vv_perf_end(vv_Ctx *ctx, vv_PerfPhaseID id) {
    (void)ctx; (void)id;
}

void vv_perf_count(vv_Ctx *ctx, vv_PerfPhaseID id) {
    (void)ctx; (void)id;
}
