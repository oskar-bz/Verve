#include "verve/vv_context.h"
#include "verve/vv_internal.h"
#include "verve/vv_layout.h"
#include "verve/vv_node.h"

#include <math.h>

// The context supplying the measure callback for the current layout run. Kept
// file-local so the recursive passes stay pool-based and terse.
static vv_Ctx *g_ctx;

// The 4-pass constraint solver (§4.1, §5). Flexbox vocabulary on Flutter-style
// box constraints: constraints down, sizes up, O(n), stable frame-to-frame.
//
//   Pass 1  bottom-up intrinsic widths  (FIT)
//   Pass 2  top-down width distribution (GROW, PERCENT, FIXED)
//   Pass 3  bottom-up heights           (text wraps here — width is final)
//   Pass 4  top-down height distribution + final positioning
//
// Width is resolved *completely* before height because text is height-for-width
// (§5.3): you can't know a paragraph's height until its width is fixed.

#define P(i) vv_pool_get(pool, (i))

static float clamp_size(vv_Size s, float v) {
    float lo = s.min;
    float hi = s.max > 0.0f ? s.max : INFINITY;
    return vv_clampf(v, lo, hi);
}

// Built-in text estimate until the backend measure callback lands (Phase 5).
// Roughly monospace-ish; good enough to exercise wrapping and height-for-width.
static vv_Vec2 builtin_text_measure(const vv_Node *n, float wrap_width) {
    const char *s = n->text;
    uint32_t len = n->text_len;
    float size = n->target.font_size > 0 ? n->target.font_size : 14.0f;
    float adv  = size * 0.5f;          // per-glyph advance estimate
    float line_h = size * 1.25f;
    float natural = (float)len * adv;
    if (wrap_width <= 0.0f || natural <= wrap_width || !s) {
        return vv_v2(natural, line_h);
    }
    int lines = (int)ceilf(natural / wrap_width);
    return vv_v2(wrap_width, line_h * (float)lines);
}

static vv_Vec2 measure_leaf(const vv_Node *n, float wrap_width) {
    if (n->measure) return n->measure(n->measure_ud, wrap_width, n);
    if (n->flags & VV_FLAG_TEXT) {
        if (g_ctx && g_ctx->measure_text) {
            const char *s = n->text;
            int len = (int)n->text_len;
            float size = n->target.font_size > 0 ? n->target.font_size : 14.0f;
            return g_ctx->measure_text(g_ctx->measure_ud, s, len, n->target.font, size, wrap_width);
        }
        return builtin_text_measure(n, wrap_width);
    }
    return vv_v2(0, 0);
}

// A child in flow (not absolutely positioned, not exiting — §3.3).
static bool in_flow(const vv_Node *c) {
    return !c->decl.has_absolute && !(c->flags & VV_FLAG_EXITING);
}

// ---- Pass 1: intrinsic widths (post-order) --------------------------------

static void pass1_width(vv_NodePool *pool, uint32_t idx) {
    vv_Node *n = P(idx);
    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling)
        pass1_width(pool, c);

    vv_Size w = n->decl.w;
    if (w.mode == VV_SIZE_FIXED) { n->fit_w = clamp_size(w, w.value); return; }

    // FIT / GROW / PERCENT all need an intrinsic estimate. GROW/PERCENT resolve
    // against the parent in pass 2, but still contribute their intrinsic to a
    // FIT ancestor, so compute it here regardless.
    float pad = n->decl.padding.l + n->decl.padding.r;
    float intrinsic;
    if (n->first_child == VV_NIL) {
        intrinsic = pad + measure_leaf(n, 0.0f).x;
    } else if (n->decl.dir == VV_ROW) {
        float sum = 0; int count = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
            if (!in_flow(P(c))) continue;
            sum += P(c)->fit_w; count++;
        }
        if (count > 1) sum += n->decl.gap * (float)(count - 1);
        intrinsic = pad + sum;
    } else { // COLUMN: cross axis is width -> max child
        float mx = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
            if (!in_flow(P(c))) continue;
            mx = vv_maxf(mx, P(c)->fit_w);
        }
        intrinsic = pad + mx;
    }
    n->fit_w = clamp_size(w, intrinsic);
}

// ---- Pass 2: width distribution (pre-order) -------------------------------

// `avail_w` is the width this node has been granted by its parent.
static void pass2_width(vv_NodePool *pool, uint32_t idx, float avail_w) {
    vv_Node *n = P(idx);
    n->layout_rect.w = avail_w;

    float content = avail_w - n->decl.padding.l - n->decl.padding.r;
    if (content < 0) content = 0;
    bool row = n->decl.dir == VV_ROW;

    // First assign each flow child a provisional width.
    float fixed_sum = 0, grow_weight = 0;
    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
        vv_Node *ch = P(c);
        // Absolute children escape flow: size comes from the absolute rect
        // (w>0), else fall back to intrinsic (tooltips that hug content).
        if (ch->decl.has_absolute) {
            ch->layout_rect.w = ch->decl.absolute.w > 0 ? ch->decl.absolute.w : ch->fit_w;
            continue;
        }
        vv_Size w = ch->decl.w;
        if (row) {
            switch (w.mode) {
                case VV_SIZE_FIXED:   ch->layout_rect.w = clamp_size(w, w.value); break;
                case VV_SIZE_PERCENT: ch->layout_rect.w = clamp_size(w, content * w.value); break;
                case VV_SIZE_GROW:    ch->layout_rect.w = clamp_size(w, w.min); grow_weight += w.value; break;
                case VV_SIZE_FIT: default: ch->layout_rect.w = ch->fit_w; break;
            }
            if (w.mode != VV_SIZE_GROW) fixed_sum += ch->layout_rect.w;
            else                        fixed_sum += ch->layout_rect.w; // min baseline
        } else { // column: width is the cross axis
            switch (w.mode) {
                case VV_SIZE_FIXED:   ch->layout_rect.w = clamp_size(w, w.value); break;
                case VV_SIZE_PERCENT: ch->layout_rect.w = clamp_size(w, content * w.value); break;
                case VV_SIZE_GROW:    ch->layout_rect.w = clamp_size(w, content); break;
                case VV_SIZE_FIT: default: ch->layout_rect.w = vv_minf(ch->fit_w, content); break;
            }
        }
    }

    // Row main-axis: distribute leftover to GROW children by weight; if we
    // overflow, shrink GROW then FIT toward their min (§5.4).
    if (row) {
        int flow = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling)
            if (in_flow(P(c))) flow++;
        float gaps = flow > 1 ? n->decl.gap * (float)(flow - 1) : 0;
        float leftover = content - fixed_sum - gaps;

        if (leftover > 0 && grow_weight > 0) {
            for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
                vv_Node *ch = P(c);
                if (!in_flow(ch) || ch->decl.w.mode != VV_SIZE_GROW) continue;
                ch->layout_rect.w += leftover * (ch->decl.w.value / grow_weight);
                ch->layout_rect.w = clamp_size(ch->decl.w, ch->layout_rect.w);
            }
        } else if (leftover < 0 && !n->decl.wrap) {
            // Overflow: shrink FIT children proportionally toward min. (GROW are
            // already at min.) Never produce negative sizes (§5.4). A wrapping
            // row instead keeps children at natural width and flows the overflow
            // onto new lines (pass 3 height + positioning), so skip shrinking.
            float shrinkable = 0;
            for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
                vv_Node *ch = P(c);
                if (in_flow(ch) && ch->decl.w.mode == VV_SIZE_FIT)
                    shrinkable += vv_maxf(0, ch->layout_rect.w - ch->decl.w.min);
            }
            if (shrinkable > 0) {
                float deficit = vv_minf(-leftover, shrinkable);
                for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
                    vv_Node *ch = P(c);
                    if (!in_flow(ch) || ch->decl.w.mode != VV_SIZE_FIT) continue;
                    float room = vv_maxf(0, ch->layout_rect.w - ch->decl.w.min);
                    ch->layout_rect.w -= deficit * (room / shrinkable);
                }
            }
        }
    }

    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling)
        pass2_width(pool, c, P(c)->layout_rect.w);
}

// ---- Pass 3: intrinsic heights (post-order) -------------------------------

static void pass3_height(vv_NodePool *pool, uint32_t idx) {
    vv_Node *n = P(idx);
    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling)
        pass3_height(pool, c);

    vv_Size h = n->decl.h;
    if (h.mode == VV_SIZE_FIXED) { n->fit_h = clamp_size(h, h.value); return; }

    float pad = n->decl.padding.t + n->decl.padding.b;
    float intrinsic;
    if (n->first_child == VV_NIL) {
        float content_w = n->layout_rect.w - n->decl.padding.l - n->decl.padding.r;
        intrinsic = pad + measure_leaf(n, content_w).y;
    } else if (n->decl.dir == VV_COLUMN) {
        float sum = 0; int count = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
            if (!in_flow(P(c))) continue;
            sum += P(c)->fit_h; count++;
        }
        if (count > 1) sum += n->decl.gap * (float)(count - 1);
        intrinsic = pad + sum;
    } else if (n->decl.wrap) { // wrapping ROW: children flow onto multiple lines
        float avail = n->layout_rect.w - n->decl.padding.l - n->decl.padding.r;
        float gap = n->decl.gap;
        float x = 0, line_h = 0, total = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
            if (!in_flow(P(c))) continue;
            float w = P(c)->layout_rect.w, chh = P(c)->fit_h;
            if (x > 0 && x + gap + w > avail) {      // doesn't fit -> new line
                total += line_h + gap;
                x = w; line_h = chh;
            } else {
                x += (x > 0 ? gap : 0) + w;
                line_h = vv_maxf(line_h, chh);
            }
        }
        intrinsic = pad + total + line_h;            // + the last line
    } else { // ROW: cross axis is height -> max child
        float mx = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
            if (!in_flow(P(c))) continue;
            mx = vv_maxf(mx, P(c)->fit_h);
        }
        intrinsic = pad + mx;
    }
    if (n->decl.aspect_ratio > 0.0f) // escape hatch (§5.3)
        intrinsic = n->layout_rect.w / n->decl.aspect_ratio;
    n->fit_h = clamp_size(h, intrinsic);
}

// ---- Pass 4: height distribution + positioning (pre-order) ----------------

static void position_children(vv_NodePool *pool, uint32_t idx);

static void pass4_height(vv_NodePool *pool, uint32_t idx, float avail_h) {
    vv_Node *n = P(idx);
    n->layout_rect.h = avail_h;

    float content = avail_h - n->decl.padding.t - n->decl.padding.b;
    if (content < 0) content = 0;
    bool col = n->decl.dir == VV_COLUMN;

    float fixed_sum = 0, grow_weight = 0;
    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
        vv_Node *ch = P(c);
        if (ch->decl.has_absolute) {
            ch->layout_rect.h = ch->decl.absolute.h > 0 ? ch->decl.absolute.h : ch->fit_h;
            continue;
        }
        vv_Size h = ch->decl.h;
        if (col) {
            switch (h.mode) {
                case VV_SIZE_FIXED:   ch->layout_rect.h = clamp_size(h, h.value); break;
                case VV_SIZE_PERCENT: ch->layout_rect.h = clamp_size(h, content * h.value); break;
                case VV_SIZE_GROW:    ch->layout_rect.h = clamp_size(h, h.min); grow_weight += h.value; break;
                case VV_SIZE_FIT: default: ch->layout_rect.h = ch->fit_h; break;
            }
            fixed_sum += ch->layout_rect.h;
        } else { // row: height is cross axis
            switch (h.mode) {
                case VV_SIZE_FIXED:   ch->layout_rect.h = clamp_size(h, h.value); break;
                case VV_SIZE_PERCENT: ch->layout_rect.h = clamp_size(h, content * h.value); break;
                case VV_SIZE_GROW:    ch->layout_rect.h = clamp_size(h, content); break;
                case VV_SIZE_FIT: default: ch->layout_rect.h = vv_minf(ch->fit_h, content); break;
            }
        }
    }

    if (col) {
        int flow = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling)
            if (in_flow(P(c))) flow++;
        float gaps = flow > 1 ? n->decl.gap * (float)(flow - 1) : 0;
        float leftover = content - fixed_sum - gaps;
        if (leftover > 0 && grow_weight > 0) {
            for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
                vv_Node *ch = P(c);
                if (!in_flow(ch) || ch->decl.h.mode != VV_SIZE_GROW) continue;
                ch->layout_rect.h += leftover * (ch->decl.h.value / grow_weight);
                ch->layout_rect.h = clamp_size(ch->decl.h, ch->layout_rect.h);
            }
        }
    }

    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling)
        pass4_height(pool, c, P(c)->layout_rect.h);
}

// Place children within this node's content box per align/gap. Runs after all
// child sizes on both axes are final. Absolute children escape flow (§5.2).
static void position_children(vv_NodePool *pool, uint32_t idx) {
    vv_Node *n = P(idx);
    vv_Edges pad = n->decl.padding;
    float ox = n->layout_rect.x + pad.l;
    float oy = n->layout_rect.y + pad.t;
    float cw = n->layout_rect.w - pad.l - pad.r;
    float ch_ = n->layout_rect.h - pad.t - pad.b;
    bool row = n->decl.dir == VV_ROW;

    // Total main-axis extent of flow children (for CENTER/END/SPACE_BETWEEN).
    float used = 0; int flow = 0;
    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
        if (!in_flow(P(c))) continue;
        used += row ? P(c)->layout_rect.w : P(c)->layout_rect.h;
        flow++;
    }
    float gap = n->decl.gap;
    float gaps_total = flow > 1 ? gap * (float)(flow - 1) : 0;
    float free_main = (row ? cw : ch_) - used - gaps_total;

    // Scroll extents (§5.6): how far content overflows the viewport. Consumed
    // by present (which glides the scroll offset) and by wheel routing.
    if (n->decl.scroll_x || n->decl.scroll_y) {
        float max_cross = 0;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
            if (!in_flow(P(c))) continue;
            max_cross = vv_maxf(max_cross, row ? P(c)->layout_rect.h : P(c)->layout_rect.w);
        }
        float content_main  = used + gaps_total;
        float content_cross = max_cross;
        float over_main  = vv_maxf(0, content_main  - (row ? cw : ch_));
        float over_cross = vv_maxf(0, content_cross - (row ? ch_ : cw));
        n->scroll_max_x = n->decl.scroll_x ? (row ? over_main : over_cross) : 0;
        n->scroll_max_y = n->decl.scroll_y ? (row ? over_cross : over_main) : 0;
    }

    // Wrapping row: flow children left-to-right, breaking to a new line when the
    // next child would overflow the content width. Left-aligned per line; the
    // vertical gap between lines matches the horizontal gap.
    if (row && n->decl.wrap) {
        float x = ox, y = oy, line_h = 0;
        bool first = true;
        for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
            vv_Node *ci = P(c);
            if (ci->decl.has_absolute || (ci->flags & VV_FLAG_EXITING)) continue;
            float w = ci->layout_rect.w, h = ci->layout_rect.h;
            if (!first && x + w > ox + cw + 0.5f) {   // wrap to next line
                x = ox; y += line_h + gap; line_h = 0;
            }
            ci->layout_rect.x = x;
            ci->layout_rect.y = y;
            x += w + gap;
            line_h = vv_maxf(line_h, h);
            first = false;
        }
        return; // absolute children in a wrap row are unusual; skip for simplicity
    }

    float cursor = row ? ox : oy;
    float extra_between = 0;
    switch (n->decl.main) {
        case VV_ALIGN_CENTER: cursor += free_main * 0.5f; break;
        case VV_ALIGN_END:    cursor += free_main; break;
        case VV_ALIGN_SPACE_BETWEEN:
            if (flow > 1) extra_between = free_main / (float)(flow - 1);
            break;
        default: break; // START
    }

    for (uint32_t c = n->first_child; c != VV_NIL; c = P(c)->next_sibling) {
        vv_Node *ch = P(c);
        if (ch->decl.has_absolute) {
            vv_Rect a = ch->decl.absolute;
            // z-lifted overlays paint in a top-level pass (§ overlays), so their
            // absolute position is window-space, not parent-relative — otherwise a
            // deeply nested popover/menu is offset by every ancestor's origin.
            float bx = ch->decl.z > 0 ? 0.0f : ox;
            float by = ch->decl.z > 0 ? 0.0f : oy;
            ch->layout_rect.x = bx + a.x;
            ch->layout_rect.y = by + a.y;
            // Keep window-space overlays (popovers, menus, dropdowns, tooltips)
            // on-screen: a popover anchored near an edge would otherwise clip at
            // the window bounds, since it isn't a real OS window. Nudge it fully
            // into view with a small margin. Skip any axis where the overlay is
            // as large as the viewport — that's a full-window scrim/backdrop,
            // which must stay put to cover everything.
            if (ch->decl.z > 0 && g_ctx) {
                const float m = 8.0f;
                float ww = g_ctx->win_w, wh = g_ctx->win_h;
                if (ch->layout_rect.w < ww)
                    ch->layout_rect.x = vv_clampf(ch->layout_rect.x, m,
                                                  ww - ch->layout_rect.w - m);
                if (ch->layout_rect.h < wh)
                    ch->layout_rect.y = vv_clampf(ch->layout_rect.y, m,
                                                  wh - ch->layout_rect.h - m);
            }
            continue;
        }
        if (ch->flags & VV_FLAG_EXITING) continue; // corpses keep last rect (§3.3)

        float main_size  = row ? ch->layout_rect.w : ch->layout_rect.h;
        float cross_size = row ? ch->layout_rect.h : ch->layout_rect.w;
        float cross_free = (row ? ch_ : cw) - cross_size;
        float cross_off = 0;
        switch (n->decl.cross) {
            case VV_ALIGN_CENTER: cross_off = cross_free * 0.5f; break;
            case VV_ALIGN_END:    cross_off = cross_free; break;
            default: break;
        }

        if (row) { ch->layout_rect.x = cursor;         ch->layout_rect.y = oy + cross_off; }
        else     { ch->layout_rect.x = ox + cross_off; ch->layout_rect.y = cursor; }

        cursor += main_size + gap + extra_between;
    }
}

// Pre-order positioning: each node's children are placed once its own rect is
// final, then we descend. prev_layout_rect is captured here for FLIP (§6.5).
static void position_tree(vv_NodePool *pool, uint32_t idx) {
    position_children(pool, idx);
    for (uint32_t c = P(idx)->first_child; c != VV_NIL; c = P(c)->next_sibling)
        position_tree(pool, c);
}

// ---- entry ----------------------------------------------------------------

// Snapshot last frame's rect as the FLIP source (§6.5) before recomputing.
static void snapshot_prev(vv_NodePool *pool, uint32_t idx) {
    P(idx)->prev_layout_rect = P(idx)->layout_rect;
    for (uint32_t c = P(idx)->first_child; c != VV_NIL; c = P(c)->next_sibling)
        snapshot_prev(pool, c);
}

void vv_layout_run(vv_Ctx *ctx) {
    vv_NodePool *pool = &ctx->pool;
    uint32_t root = ctx->root;
    vv_Rect window = vv_rect(0, 0, ctx->win_w, ctx->win_h);
    g_ctx = ctx;

    snapshot_prev(pool, root);

    vv_Node *r = P(root);
    r->layout_rect = window;      // root fills the window; position at its origin
    pass1_width(pool, root);
    pass2_width(pool, root, window.w);
    pass3_height(pool, root);
    pass4_height(pool, root, window.h);
    position_tree(pool, root);

    g_ctx = NULL;
}
