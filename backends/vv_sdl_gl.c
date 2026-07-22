#include "vv_sdl_gl.h"
#include "vv_vector.h"

#include <SDL3/SDL.h>
#include <epoxy/gl.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Text, icons and dynamic SVG all rasterize on the CPU through craz (../craz):
// analytic-AA fills/strokes, a subpixel glyph cache, and a skyline atlas packer.
// craz owns outline -> coverage; Verve keeps shaping/wrapping. Glyph masks live
// in craz's A8 atlas and are mirrored into a GL texture (dirty-rect uploads).
#include "craz/craz.h"
#include "craz/font.h"
#include "craz/bake.h"

// craz's font.o only references the stbtt_* symbols; it leaves the implementation
// to the consumer so a program can share one copy. This backend TU is that copy.
// The vendored header is byte-identical to craz's (both v1.26), so struct layouts
// match. (This must be the only STB_TRUETYPE_IMPLEMENTATION in a GUI binary.)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// Ship the drop-in devtools (node inspector + performance HUD) as part of the
// backend: their single-header implementations compile here once, so vv_app_run
// can attach them behind vv_AppDesc.devtools and any app can link them without a
// second copy. They are backend-agnostic (core-only) overlays.
#define VV_INSPECT_IMPL
#include "vv_inspect.h"
#define VV_PERF_HUD_IMPL
#include "vv_perf_hud.h"

// One backend, two shader programs: a rounded-box SDF for rects (fill + border +
// shadow + AA in one pass, §9.1) and a textured-quad shader for glyphs.

#define GLYPH_ATLAS_W 2048
#define GLYPH_ATLAS_H 2048
#define SUBPIXEL_PHASES 4      // 1 = pixel-snapped, 3-4 = smooth subpixel text
#define MAX_FONTS 8
#define MAX_SCISSORS 64

// A loaded face: a craz font plus its subpixel glyph cache — an A8 atlas keyed
// by (glyph, size, horizontal phase). Baking happens on cache miss with craz's
// analytic-AA rasterizer, so text stays crisp at every size without a fixed
// bake size to scale from. The cache's A8 pixels are mirrored into `atlas`.
typedef struct {
    bool            loaded;
    cr_font        *font;
    cr_glyph_cache *cache;
    GLuint          atlas;              // GL texture mirroring the A8 cache
    int             atlas_w, atlas_h;
    // Retained TTF + stbtt view of the same face, used for pair kerning (craz
    // owns coverage only and leaves shaping/kerning to the consumer). NULL/false
    // when the face has no usable kern table.
    unsigned char  *ttf;
    stbtt_fontinfo  info;
    bool            has_info;
} Font;

// Shared across a window and its GL-sharing children: the font table and one
// transient context used to bake glyphs on cache miss.
typedef struct {
    Font        fonts[MAX_FONTS];
    int         font_count;
    cr_context *bake;                  // transient glyph-bake scratch
} FontSystem;

struct vv_App {
    SDL_Window   *win;
    SDL_GLContext gl;
    SDL_WindowID  win_id;   // for routing events in the multi-window pump
    vv_Backend    backend;

    GLuint rect_prog, text_prog, poly_prog, image_prog;
    GLuint vao, vbo;
    GLint  rect_u_vp, text_u_vp, text_u_tex, poly_u_vp, image_u_vp, image_u_tex;

    FontSystem *fs;         // shared with GL-sharing children
    bool   owns_fs;         // root window frees it
    bool   shares_gl;       // child window: programs/atlas belong to the parent

    vv_Rect scissors[MAX_SCISSORS];
    int     scissor_top;

    int   fb_w, fb_h;
    float dpi;

    // Multi-window pump state (§ open-question 2). Each window owns its input.
    vv_Input input;
    bool     should_close;

    char       *clip_cache;   // last clipboard text (freed on next get)
    SDL_Cursor *cursors[8];   // lazily created system cursors, by vv_CursorShape

    vv_Vector  *vec;          // craz-backed icons/SVG/canvas (lazily created)

    // Dynamic vertex scratch (grows).
    float   *verts;
    size_t   vcap, vcount; // in floats
};

// Registry of open windows, so one pump call can route SDL events to the right
// window by ID. Small fixed cap — a UI with more live windows is unusual.
#define MAX_WINDOWS 16
static vv_App *g_wins[MAX_WINDOWS];
static int     g_nwin;

static void reg_window(vv_App *a) {
    if (g_nwin < MAX_WINDOWS) g_wins[g_nwin++] = a;
}
static void unreg_window(vv_App *a) {
    for (int i = 0; i < g_nwin; i++)
        if (g_wins[i] == a) { g_wins[i] = g_wins[--g_nwin]; return; }
}
static vv_App *win_by_id(SDL_WindowID id) {
    for (int i = 0; i < g_nwin; i++)
        if (g_wins[i]->win_id == id) return g_wins[i];
    return NULL;
}

// ---- gl helpers -----------------------------------------------------------

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log);
               fprintf(stderr, "shader compile error:\n%s\n", log); }
    return s;
}
static GLuint link_prog(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, sizeof log, NULL, log);
               fprintf(stderr, "link error:\n%s\n", log); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// Rect shader. Per-vertex: screen pos, local coord (px from center), half-size,
// per-corner radius, fill, border color, border width, edge softness (blur/AA).
static const char *RECT_VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_local;\n"
    "layout(location=2) in vec2 a_half;\n"
    "layout(location=3) in vec4 a_radius;\n"
    "layout(location=4) in vec4 a_fill;\n"
    "layout(location=5) in vec4 a_border;\n"
    "layout(location=6) in float a_bw;\n"
    "layout(location=7) in float a_soft;\n"
    "uniform vec2 u_vp;\n"
    "out vec2 v_local; out vec2 v_half; out vec4 v_radius;\n"
    "out vec4 v_fill; out vec4 v_border; out float v_bw; out float v_soft;\n"
    "void main(){\n"
    "  v_local=a_local; v_half=a_half; v_radius=a_radius;\n"
    "  v_fill=a_fill; v_border=a_border; v_bw=a_bw; v_soft=a_soft;\n"
    "  vec2 ndc = vec2(a_pos.x/u_vp.x*2.0-1.0, 1.0-a_pos.y/u_vp.y*2.0);\n"
    "  gl_Position=vec4(ndc,0.0,1.0);\n"
    "}\n";

static const char *RECT_FS =
    "#version 330 core\n"
    "in vec2 v_local; in vec2 v_half; in vec4 v_radius;\n"
    "in vec4 v_fill; in vec4 v_border; in float v_bw; in float v_soft;\n"
    "out vec4 frag;\n"
    "float sd_round_box(vec2 p, vec2 b, vec4 r){\n"
    "  r.xy = (p.x>0.0)?r.xy:r.zw;\n"     // pick left/right pair
    "  r.x  = (p.y>0.0)?r.x:r.y;\n"       // pick top/bottom
    "  r.x  = min(r.x, min(b.x, b.y));\n" // clamp to a pill (radius_full=9999 else degenerates)
    "  vec2 q = abs(p)-b+r.x;\n"
    "  return min(max(q.x,q.y),0.0)+length(max(q,vec2(0.0)))-r.x;\n"
    "}\n"
    "void main(){\n"
    // radius order tl,tr,br,bl -> shader wants (x=+x+y? ) map: right pair (tr,br), left (tl,bl)
    "  vec4 r = vec4(v_radius.y, v_radius.z, v_radius.x, v_radius.w);\n"
    "  float d = sd_round_box(v_local, v_half, r);\n"
    "  float aa = fwidth(d) + v_soft;\n"
    "  float shape = smoothstep(aa, -aa, d);\n"
    "  vec4 col = v_fill;\n"
    "  if(v_bw > 0.0){\n"
    "    float bd = abs(d + v_bw*0.5) - v_bw*0.5;\n"
    "    float bm = smoothstep(aa, -aa, bd);\n"
    "    col = mix(col, v_border, bm);\n"
    "  }\n"
    "  frag = vec4(col.rgb, col.a*shape);\n"
    "}\n";

static const char *TEXT_VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_color;\n"
    "uniform vec2 u_vp;\n"
    "out vec2 v_uv; out vec4 v_color;\n"
    "void main(){ v_uv=a_uv; v_color=a_color;\n"
    "  vec2 ndc=vec2(a_pos.x/u_vp.x*2.0-1.0, 1.0-a_pos.y/u_vp.y*2.0);\n"
    "  gl_Position=vec4(ndc,0.0,1.0);}\n";

static const char *TEXT_FS =
    "#version 330 core\n"
    "in vec2 v_uv; in vec4 v_color; out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ float a=texture(u_tex,v_uv).r; frag=vec4(v_color.rgb, v_color.a*a); }\n";

// Textured quads (VV_CMD_IMAGE): same vertex layout as text (pos2+uv2+color4),
// but samples full RGBA and multiplies by the tint.
static const char *IMAGE_FS =
    "#version 330 core\n"
    "in vec2 v_uv; in vec4 v_color; out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ frag = texture(u_tex, v_uv) * v_color; }\n";

// Solid triangles — the vector primitive (VV_CMD_POLY). Same logical->NDC map
// as the rect/text shaders; colour comes per-vertex, no SDF.
static const char *POLY_VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_color;\n"
    "uniform vec2 u_vp;\n"
    "out vec4 v_color;\n"
    "void main(){ v_color=a_color;\n"
    "  vec2 ndc=vec2(a_pos.x/u_vp.x*2.0-1.0, 1.0-a_pos.y/u_vp.y*2.0);\n"
    "  gl_Position=vec4(ndc,0.0,1.0); }\n";

static const char *POLY_FS =
    "#version 330 core\n"
    "in vec4 v_color; out vec4 frag;\n"
    "void main(){ frag=v_color; }\n";

// ---- vertex scratch -------------------------------------------------------

static void vpush(vv_App *a, size_t n) {
    if (a->vcount + n > a->vcap) {
        size_t nc = a->vcap ? a->vcap * 2 : 4096;
        while (nc < a->vcount + n) nc *= 2;
        a->verts = realloc(a->verts, nc * sizeof(float));
        a->vcap = nc;
    }
}

// ---- font + dynamic glyph atlas -------------------------------------------

static Font *font_of(FontSystem *fs, vv_FontID id) {
    if (id < (vv_FontID)fs->font_count && fs->fonts[id].loaded) return &fs->fonts[id];
    return fs->font_count > 0 && fs->fonts[0].loaded ? &fs->fonts[0] : NULL;
}

// Pick the font that actually has `cp`, preferring `pref` then falling back
// across the loaded faces — so a base face lacking CJK/symbols still renders it
// via a later face. Returns the preferred index if none has the glyph.
static int resolve_font(FontSystem *fs, vv_FontID pref, uint32_t cp) {
    int p = (pref < (vv_FontID)fs->font_count && fs->fonts[pref].loaded) ? (int)pref : 0;
    if (fs->fonts[p].loaded && cr_font_glyph_index(fs->fonts[p].font, (int)cp)) return p;
    for (int i = 0; i < fs->font_count; i++)
        if (fs->fonts[i].loaded && cr_font_glyph_index(fs->fonts[i].font, (int)cp)) return i;
    return p;
}

// Ascent in pixels for a face at `size` — the baseline offset from a line's top.
static float ascent_px(const Font *f, float size) {
    float sc = cr_font_scale_for_pixels(f->font, size);
    float asc = 0, desc = 0, gap = 0;
    cr_font_vmetrics(f->font, sc, &asc, &desc, &gap);
    return asc;
}

// Lazily create the GL texture that mirrors a font's A8 glyph atlas.
static void ensure_glyph_atlas(Font *f) {
    if (f->atlas) return;
    int w = 0, h = 0, stride = 0;
    cr_glyph_cache_pixels(f->cache, &w, &h, &stride);
    f->atlas_w = w; f->atlas_h = h;
    glGenTextures(1, &f->atlas);
    glBindTexture(GL_TEXTURE_2D, f->atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Upload only the atlas region baked since the last sync (craz tracks a dirty
// rect), so steady-state text re-uploads nothing.
static void sync_glyph_atlas(Font *f) {
    if (!f->atlas) return;
    cr_rect d;
    if (!cr_glyph_cache_take_dirty(f->cache, &d) || d.w <= 0 || d.h <= 0) return;
    int w = 0, h = 0, stride = 0;
    const uint8_t *px = cr_glyph_cache_pixels(f->cache, &w, &h, &stride);
    glBindTexture(GL_TEXTURE_2D, f->atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    glTexSubImage2D(GL_TEXTURE_2D, 0, d.x, d.y, d.w, d.h, GL_RED, GL_UNSIGNED_BYTE,
                    px + (size_t)d.y * stride + d.x);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

vv_FontID vv_app_load_font(vv_App *a, const char *path) {
    FontSystem *fs = a->fs;
    if (fs->font_count >= MAX_FONTS) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "font: cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    unsigned char *ttf = malloc((size_t)sz);
    if (fread(ttf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(ttf); return 0; }
    fclose(f);

    cr_font *cf = cr_font_load(ttf, (int)sz, 0);  // craz copies the font data
    if (!cf) { free(ttf); return 0; }

    Font *fo = &fs->fonts[fs->font_count];
    fo->font  = cf;
    fo->cache = cr_glyph_cache_new(cf, GLYPH_ATLAS_W, GLYPH_ATLAS_H, SUBPIXEL_PHASES);
    fo->loaded = fo->cache != NULL;
    if (!fo->loaded) { cr_font_free(cf); free(ttf); return 0; }
    // Keep our own TTF copy alive for stbtt pair kerning (see glyph_pair_kern).
    fo->ttf = ttf;
    fo->has_info = stbtt_InitFont(&fo->info, ttf,
                                  stbtt_GetFontOffsetForIndex(ttf, 0)) != 0;
    if (!fs->bake) fs->bake = cr_context_new();
    return (vv_FontID)fs->font_count++;
}

// True for scripts written without spaces, so we allow a line break after each
// such codepoint (CJK, kana, Hangul, fullwidth) — a reduced UAX #14.
static bool is_cjk(uint32_t cp) {
    return (cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Pen advance (px) for a codepoint at `size`, via its resolved face's cache.
// Baking on miss is deliberate — measuring warms the cache the emit pass reuses.
static float glyph_advance(FontSystem *fs, vv_FontID font, uint32_t cp, float size) {
    int fi = resolve_font(fs, font, cp);
    cr_glyph_entry e;
    if (cr_glyph_cache_get(fs->fonts[fi].cache, fs->bake, (int)cp, size, 0.0f, &e))
        return e.advance;
    return 0;
}

// Pair-kerning adjustment (device px) between two codepoints on the same face,
// from the font's legacy `kern` table via stbtt. craz supplies per-glyph advances
// but no kerning, so without this pairs like "To", "AV", "r." render too loose.
// Returns 0 when the face has no kern data or either glyph is missing.
static float glyph_pair_kern(FontSystem *fs, int fi, uint32_t prev, uint32_t cp,
                             float dsize) {
    Font *f = &fs->fonts[fi];
    if (!f->has_info || !prev) return 0;
    int g1 = stbtt_FindGlyphIndex(&f->info, (int)prev);
    int g2 = stbtt_FindGlyphIndex(&f->info, (int)cp);
    if (!g1 || !g2) return 0;
    int k = stbtt_GetGlyphKernAdvance(&f->info, g1, g2);
    if (!k) return 0;
    return (float)k * stbtt_ScaleForPixelHeight(&f->info, dsize);
}

// Walk a UTF-8 string, measuring (emit_font < 0) or emitting textured quads.
// Glyph coverage lives in the resolved face's A8 atlas and is tinted on the GPU
// by the per-vertex colour, so the same masks serve any colour — no re-bake for
// hover/disabled/theme. When emitting we output only glyphs whose resolved font
// == emit_font, so draw_text can bind one atlas per pass. Wraps on spaces
// (words) and after CJK codepoints; '\n' forces a line.
//
// Legibility: all layout runs in *device* pixels (logical * dpi), so glyphs are
// baked and cached at the resolution they're displayed at — no upscaling blur on
// HiDPI. Each glyph's device origin is pixel-snapped (x floored with a subpixel
// phase for smooth horizontal spacing; baseline rounded to a whole device row),
// then the quad is converted back to logical space for the shader.
static vv_Vec2 text_layout(vv_App *a, vv_FontID font, const char *s, int len,
                           float size, float wrap, float ox, float oy,
                           vv_Color col, int emit_font) {
    FontSystem *fs = a->fs;
    Font *pf = font_of(fs, font);
    if (!pf) return vv_v2(0, 0);
    float dpi = a->dpi > 0.0f ? a->dpi : 1.0f;
    float dsize = size * dpi;                 // device-pixel bake/layout size
    float line_h = size * 1.25f * dpi;        // device px per line
    float baseline = ascent_px(pf, dsize);    // device px, line top -> baseline
    float wrap_d = wrap * dpi;
    float ox_d = ox * dpi, oy_d = oy * dpi;
    float x = 0, y = 0, maxw = 0;             // device px along the current line
    int i = 0;
    uint32_t prev_cp = 0; int prev_fi = -1;   // last glyph on the line, for kerning
    while (i < len) {
        int adv = 1;
        uint32_t cp = vv_utf8_decode(s + i, &adv);

        if (cp == '\n') { maxw = fmaxf(maxw, x); x = 0; y += line_h;
                          prev_cp = 0; prev_fi = -1; i += adv; continue; }

        // Determine this chunk: a run up to the next break opportunity. A space
        // is its own chunk; a CJK codepoint is its own chunk; otherwise a word.
        int chunk_start = i, j = i;
        float chunk_w = 0;
        if (cp == ' ') {
            chunk_w = glyph_advance(fs, font, ' ', dsize); j += adv;
        } else if (is_cjk(cp)) {
            chunk_w = glyph_advance(fs, font, cp, dsize); j += adv;
        } else {
            uint32_t wprev = 0; int wpfi = -1;
            while (j < len) {
                int a2 = 1; uint32_t c2 = vv_utf8_decode(s + j, &a2);
                if (c2 == ' ' || c2 == '\n' || is_cjk(c2)) break;
                int cfi = resolve_font(fs, font, c2);
                if (wpfi == cfi) chunk_w += glyph_pair_kern(fs, cfi, wprev, c2, dsize);
                chunk_w += glyph_advance(fs, font, c2, dsize);
                wprev = c2; wpfi = cfi;
                j += a2;
            }
        }
        // Wrap before this chunk if it overflows (but never break a leading chunk).
        // The 0.5px slack keeps text from wrapping at *exactly* its own natural
        // width: measuring sums advances one-by-one while this check sums the
        // chunk separately, so float non-associativity can push (x+chunk_w) an
        // ULP past `wrap` — a spurious second line whose taller box then shifts
        // cross-centered text. Sub-pixel slack is invisible for genuine overflow.
        bool breakable = cp != ' ';
        if (wrap_d > 0 && x > 0 && breakable && (x + chunk_w) > wrap_d + 0.5f) {
            maxw = fmaxf(maxw, x); x = 0; y += line_h; prev_cp = 0; prev_fi = -1;
        }

        // Emit / advance the chunk's glyphs.
        for (int k = chunk_start; k < j;) {
            int a2 = 1; uint32_t c2 = vv_utf8_decode(s + k, &a2);
            int fi = resolve_font(fs, font, c2);
            Font *ef = &fs->fonts[fi];
            // Tuck against the previous glyph on this line (same face only).
            if (prev_fi == fi) x += glyph_pair_kern(fs, fi, prev_cp, c2, dsize);
            prev_cp = c2; prev_fi = fi;
            float penx = ox_d + x;
            cr_glyph_entry e;
            if (cr_glyph_cache_get(ef->cache, fs->bake, (int)c2, dsize, penx, &e)) {
                if (emit_font == fi && e.w > 0) {
                    // Pixel-snapped device origin, then back to logical for the shader.
                    float bl = floorf(oy_d + y + baseline + 0.5f);
                    float x0 = (floorf(penx) + (float)e.bearing_x) / dpi;
                    float y0 = (bl + (float)e.bearing_y) / dpi;
                    float x1 = x0 + (float)e.w / dpi, y1 = y0 + (float)e.h / dpi;
                    float u0 = (float)e.x / ef->atlas_w, v0 = (float)e.y / ef->atlas_h;
                    float u1 = (float)(e.x + e.w) / ef->atlas_w, v1 = (float)(e.y + e.h) / ef->atlas_h;
                    float verts[6][8] = {
                        {x0,y0, u0,v0, col.r,col.g,col.b,col.a},
                        {x1,y0, u1,v0, col.r,col.g,col.b,col.a},
                        {x1,y1, u1,v1, col.r,col.g,col.b,col.a},
                        {x0,y0, u0,v0, col.r,col.g,col.b,col.a},
                        {x1,y1, u1,v1, col.r,col.g,col.b,col.a},
                        {x0,y1, u0,v1, col.r,col.g,col.b,col.a},
                    };
                    vpush(a, 6 * 8);
                    memcpy(a->verts + a->vcount, verts, sizeof verts);
                    a->vcount += 6 * 8;
                }
                x += e.advance;
            }
            k += a2;
        }
        maxw = fmaxf(maxw, x);
        i = j;
    }
    // Return logical extent (device layout / dpi).
    maxw = fmaxf(maxw, x);
    return vv_v2(maxw / dpi, (y + line_h) / dpi);
}

vv_Vec2 vv_app_measure(void *ud, const char *s, int len, vv_FontID font,
                       float size, float wrap_width) {
    vv_App *a = ud;
    return text_layout(a, font, s, len, size, wrap_width, 0, 0, (vv_Color){0}, -1);
}

// ---- backend vtable -------------------------------------------------------

static void set_scissor(vv_App *a) {
    if (a->scissor_top == 0) { glDisable(GL_SCISSOR_TEST); return; }
    vv_Rect r = a->scissors[a->scissor_top - 1];
    glEnable(GL_SCISSOR_TEST);
    // GL scissor origin is bottom-left; our rects are top-left.
    int y = a->fb_h - (int)((r.y + r.h) * a->dpi);
    glScissor((int)(r.x * a->dpi), y, (int)(r.w * a->dpi), (int)(r.h * a->dpi));
}

// Vertex layout (20 floats): pos2, local2, half2, radius4, fill4, border4, bw, soft.
static void emit_rect_quad(vv_App *a, vv_Rect r, vv_Corners rad, vv_Color fill,
                           vv_Color border, float bw, float soft) {
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    float hx = r.w * 0.5f, hy = r.h * 0.5f;
    float ex = hx + soft + 1.5f, ey = hy + soft + 1.5f; // expand for AA/blur feather
    float corners[4][2] = {{-ex,-ey},{ex,-ey},{ex,ey},{-ex,ey}};
    int idx[6] = {0,1,2,0,2,3};
    for (int i = 0; i < 6; i++) {
        float lx = corners[idx[i]][0], ly = corners[idx[i]][1];
        vpush(a, 20);
        float *o = a->verts + a->vcount;
        o[0]=cx+lx; o[1]=cy+ly; o[2]=lx; o[3]=ly; o[4]=hx; o[5]=hy;
        o[6]=rad.tl; o[7]=rad.tr; o[8]=rad.br; o[9]=rad.bl;
        o[10]=fill.r; o[11]=fill.g; o[12]=fill.b; o[13]=fill.a;
        o[14]=border.r; o[15]=border.g; o[16]=border.b; o[17]=border.a;
        o[18]=bw; o[19]=soft;
        a->vcount += 20;
    }
}

static void draw_rects(void *ctx, const vv_CmdRect *rects, int n) {
    vv_App *a = ctx;
    a->vcount = 0;
    for (int i = 0; i < n; i++) {
        const vv_CmdRect *c = &rects[i];
        if (c->shadow.color.a > 0.001f && c->shadow.blur >= 0.0f &&
            (c->shadow.color.a > 0)) {
            vv_Rect sr = { c->rect.x + c->shadow.offset.x - c->shadow.spread,
                           c->rect.y + c->shadow.offset.y - c->shadow.spread,
                           c->rect.w + c->shadow.spread * 2,
                           c->rect.h + c->shadow.spread * 2 };
            emit_rect_quad(a, sr, c->radius, c->shadow.color,
                           (vv_Color){0}, 0.0f, fmaxf(c->shadow.blur, 0.5f));
        }
        float bw = fmaxf(fmaxf(c->border_width.l, c->border_width.t),
                         fmaxf(c->border_width.r, c->border_width.b));
        emit_rect_quad(a, c->rect, c->radius, c->fill_a, c->border_color, bw, 0.0f);
    }
    if (a->vcount == 0) return;

    glUseProgram(a->rect_prog);
    glUniform2f(a->rect_u_vp, (float)a->fb_w / a->dpi, (float)a->fb_h / a->dpi);
    glBindVertexArray(a->vao);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(a->vcount * sizeof(float)), a->verts, GL_STREAM_DRAW);
    GLsizei stride = 20 * sizeof(float);
    for (int i = 0; i < 8; i++) glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)(0*sizeof(float)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(6*sizeof(float)));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(10*sizeof(float)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)(14*sizeof(float)));
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)(18*sizeof(float)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)(19*sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(a->vcount / 20));
}

static void draw_text(void *ctx, const vv_CmdText *runs, int n) {
    vv_App *a = ctx;
    FontSystem *fs = a->fs;
    if (fs->font_count == 0 || n == 0) return;

    // Bake pass: lay out every run once (emit_font < 0) so all glyphs are baked
    // into their caches, then create/refresh each font's GL atlas from the
    // dirty region. Doing this before the emit passes avoids a one-frame gap for
    // fallback glyphs that a later pass would otherwise bake after its upload.
    for (int i = 0; i < n; i++) {
        const vv_CmdText *t = &runs[i];
        Font *pf = font_of(fs, t->font);
        if (!pf) continue;
        float top = t->origin.y - ascent_px(pf, t->size); // baseline -> line top
        text_layout(a, t->font, t->utf8, (int)t->len, t->size, 0,
                    t->origin.x, top, t->color, -1);
    }
    for (int fi = 0; fi < fs->font_count; fi++)
        if (fs->fonts[fi].loaded) { ensure_glyph_atlas(&fs->fonts[fi]); sync_glyph_atlas(&fs->fonts[fi]); }

    // Emit + draw one face at a time, so each pass binds a single atlas texture.
    glUseProgram(a->text_prog);
    glUniform2f(a->text_u_vp, (float)a->fb_w / a->dpi, (float)a->fb_h / a->dpi);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(a->text_u_tex, 0);
    glBindVertexArray(a->vao);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo);
    GLsizei stride = 8 * sizeof(float);
    for (int i = 0; i < 3; i++) glEnableVertexAttribArray((GLuint)i);
    for (int fi = 0; fi < fs->font_count; fi++) {
        if (!fs->fonts[fi].loaded) continue;
        a->vcount = 0;
        for (int i = 0; i < n; i++) {
            const vv_CmdText *t = &runs[i];
            Font *pf = font_of(fs, t->font);
            if (!pf) continue;
            float top = t->origin.y - ascent_px(pf, t->size);
            text_layout(a, t->font, t->utf8, (int)t->len, t->size, 0,
                        t->origin.x, top, t->color, fi);
        }
        if (a->vcount == 0) continue;
        glBindTexture(GL_TEXTURE_2D, fs->fonts[fi].atlas);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(a->vcount * sizeof(float)), a->verts, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(a->vcount / 8));
    }
}

// ---- vector polys (VV_CMD_POLY) -------------------------------------------
// Solid triangles in logical coords (the poly shader divides by u_vp). Strokes
// become a quad per segment plus a disc at every vertex (round joins + caps);
// fills are a triangle fan; points are discs. Edges are antialiased by fringing
// them with a ~1px feather band whose outer vertices fade to alpha 0 — the
// per-vertex-colour shader gives the gradient for free, no SDF pass.

// One solid vertex: pos2 + color4.
static void pv(vv_App *a, float x, float y, vv_Color c) {
    vpush(a, 6);
    float *o = a->verts + a->vcount;
    o[0] = x; o[1] = y; o[2] = c.r; o[3] = c.g; o[4] = c.b; o[5] = c.a;
    a->vcount += 6;
}
static void ptri3(vv_App *a, vv_Vec2 p0, vv_Color c0, vv_Vec2 p1, vv_Color c1,
                  vv_Vec2 p2, vv_Color c2) {
    pv(a, p0.x, p0.y, c0); pv(a, p1.x, p1.y, c1); pv(a, p2.x, p2.y, c2);
}
static void ptri(vv_App *a, vv_Vec2 p0, vv_Vec2 p1, vv_Vec2 p2, vv_Color c) {
    ptri3(a, p0, c, p1, c, p2, c);
}
// A quad (inner edge i0->i1, outer edge o1->o0) with per-edge colour: two tris.
static void pquad2(vv_App *a, vv_Vec2 i0, vv_Vec2 i1, vv_Vec2 o1, vv_Vec2 o0,
                   vv_Color ci, vv_Color co) {
    ptri3(a, i0, ci, i1, ci, o1, co);
    ptri3(a, i0, ci, o1, co, o0, co);
}
// Antialiased disc: solid fan to r, plus a feather ring r -> r+aa fading out.
static void pdisc(vv_App *a, vv_Vec2 ctr, float r, float aa, vv_Color c) {
    if (r <= 0.0f) return;
    vv_Color c0 = {c.r, c.g, c.b, 0.0f};
    const int SEG = 16;
    for (int i = 0; i < SEG; i++) {
        float t0 = (float)i / SEG * 6.2831853f, t1 = (float)(i + 1) / SEG * 6.2831853f;
        vv_Vec2 d0 = {cosf(t0), sinf(t0)}, d1 = {cosf(t1), sinf(t1)};
        vv_Vec2 p0 = {ctr.x + d0.x * r, ctr.y + d0.y * r};
        vv_Vec2 p1 = {ctr.x + d1.x * r, ctr.y + d1.y * r};
        ptri(a, ctr, p0, p1, c);
        vv_Vec2 q0 = {ctr.x + d0.x * (r + aa), ctr.y + d0.y * (r + aa)};
        vv_Vec2 q1 = {ctr.x + d1.x * (r + aa), ctr.y + d1.y * (r + aa)};
        pquad2(a, p0, p1, q1, q0, c, c0);
    }
}
// Antialiased segment: solid core quad at ±hw, feather bands out to ±(hw+aa).
static void pseg(vv_App *a, vv_Vec2 p0, vv_Vec2 p1, float hw, float aa, vv_Color c) {
    float dx = p1.x - p0.x, dy = p1.y - p0.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len, ny = dx / len; // unit perpendicular
    vv_Color c0 = {c.r, c.g, c.b, 0.0f};
    vv_Vec2 a_in = {p0.x + nx * hw, p0.y + ny * hw}, b_in = {p1.x + nx * hw, p1.y + ny * hw};
    vv_Vec2 a_ni = {p0.x - nx * hw, p0.y - ny * hw}, b_ni = {p1.x - nx * hw, p1.y - ny * hw};
    ptri(a, a_in, b_in, b_ni, c); ptri(a, a_in, b_ni, a_ni, c);      // core
    float ho = hw + aa;
    vv_Vec2 a_out = {p0.x + nx * ho, p0.y + ny * ho}, b_out = {p1.x + nx * ho, p1.y + ny * ho};
    vv_Vec2 a_no = {p0.x - nx * ho, p0.y - ny * ho}, b_no = {p1.x - nx * ho, p1.y - ny * ho};
    pquad2(a, a_in, b_in, b_out, a_out, c, c0);  // + side feather
    pquad2(a, a_ni, b_ni, b_no, a_no, c, c0);    // - side feather
}

static void draw_polys(void *ctx, const vv_CmdPoly *polys, int n) {
    vv_App *a = ctx;
    a->vcount = 0;
    float aa = 1.0f / (a->dpi > 0.0f ? a->dpi : 1.0f); // ~1 device px, in logical units
    for (int i = 0; i < n; i++) {
        const vv_CmdPoly *p = &polys[i];
        if (p->count == 0) continue;
        vv_Vec2 o = p->origin;
        vv_Color col = p->color;
        if (p->flags & VV_POLY_POINTS) {
            float r = p->width * 0.5f;
            for (uint32_t k = 0; k < p->count; k++)
                pdisc(a, vv_v2(o.x + p->pts[k].x, o.y + p->pts[k].y), r, aa, col);
        } else if (p->flags & VV_POLY_FILL) {
            for (uint32_t k = 1; k + 1 < p->count; k++)
                ptri(a, vv_v2(o.x + p->pts[0].x, o.y + p->pts[0].y),
                        vv_v2(o.x + p->pts[k].x, o.y + p->pts[k].y),
                        vv_v2(o.x + p->pts[k + 1].x, o.y + p->pts[k + 1].y), col);
        } else {
            float hw = fmaxf(p->width, 0.5f) * 0.5f;
            uint32_t segs = (p->flags & VV_POLY_CLOSED) ? p->count : p->count - 1;
            for (uint32_t k = 0; k < segs; k++) {
                vv_Vec2 aP = {o.x + p->pts[k].x, o.y + p->pts[k].y};
                vv_Vec2 bP = {o.x + p->pts[(k + 1) % p->count].x,
                              o.y + p->pts[(k + 1) % p->count].y};
                pseg(a, aP, bP, hw, aa, col);
            }
            // Round joins + caps: a disc at every vertex (skip for hairlines).
            if (hw > 0.75f)
                for (uint32_t k = 0; k < p->count; k++)
                    pdisc(a, vv_v2(o.x + p->pts[k].x, o.y + p->pts[k].y), hw, aa, col);
        }
    }
    if (a->vcount == 0) return;

    glUseProgram(a->poly_prog);
    glUniform2f(a->poly_u_vp, (float)a->fb_w / a->dpi, (float)a->fb_h / a->dpi);
    glBindVertexArray(a->vao);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(a->vcount * sizeof(float)), a->verts, GL_STREAM_DRAW);
    GLsizei stride = 6 * sizeof(float);
    for (int i = 0; i < 2; i++) glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(a->vcount / 6));
}

// ---- images (VV_CMD_IMAGE) ------------------------------------------------
static vv_TexID backend_tex_create(void *ctx, const void *px, int w, int h, vv_PixFmt fmt) {
    (void)ctx;
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (fmt == VV_PIXFMT_A8)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return (vv_TexID)tex;
}
static void backend_tex_destroy(void *ctx, vv_TexID t) {
    (void)ctx; GLuint g = (GLuint)t; glDeleteTextures(1, &g);
}
static void draw_image(void *ctx, const vv_CmdImage *imgs, int n) {
    vv_App *a = ctx;
    for (int i = 0; i < n; i++) {
        const vv_CmdImage *im = &imgs[i];
        vv_Rect r = im->rect, uv = im->uv;
        if (uv.w == 0 && uv.h == 0) uv = (vv_Rect){0, 0, 1, 1};
        vv_Color c = im->tint.a > 0 ? im->tint : (vv_Color){1, 1, 1, 1};
        float v[6][8] = {
            {r.x,       r.y,       uv.x,        uv.y,        c.r, c.g, c.b, c.a},
            {r.x + r.w, r.y,       uv.x + uv.w, uv.y,        c.r, c.g, c.b, c.a},
            {r.x + r.w, r.y + r.h, uv.x + uv.w, uv.y + uv.h, c.r, c.g, c.b, c.a},
            {r.x,       r.y,       uv.x,        uv.y,        c.r, c.g, c.b, c.a},
            {r.x + r.w, r.y + r.h, uv.x + uv.w, uv.y + uv.h, c.r, c.g, c.b, c.a},
            {r.x,       r.y + r.h, uv.x,        uv.y + uv.h, c.r, c.g, c.b, c.a},
        };
        a->vcount = 0; vpush(a, 6 * 8);
        memcpy(a->verts + a->vcount, v, sizeof v); a->vcount += 6 * 8;

        glUseProgram(a->image_prog);
        glUniform2f(a->image_u_vp, (float)a->fb_w / a->dpi, (float)a->fb_h / a->dpi);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)im->tex);
        glUniform1i(a->image_u_tex, 0);
        glBindVertexArray(a->vao);
        glBindBuffer(GL_ARRAY_BUFFER, a->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(a->vcount * sizeof(float)), a->verts, GL_STREAM_DRAW);
        GLsizei stride = 8 * sizeof(float);
        for (int k = 0; k < 3; k++) glEnableVertexAttribArray((GLuint)k);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
}

// Public: upload RGBA8 pixels and return a texture id usable in a vv_ImageRef.
vv_TexID vv_app_texture_from_rgba(vv_App *a, const void *rgba, int w, int h) {
    SDL_GL_MakeCurrent(a->win, a->gl);
    return backend_tex_create(a, rgba, w, h, VV_PIXFMT_RGBA8);
}
void vv_app_texture_destroy(vv_App *a, vv_TexID t) { backend_tex_destroy(a, t); }

static void push_scissor(void *ctx, vv_Rect r) {
    vv_App *a = ctx;
    if (a->scissor_top < MAX_SCISSORS) a->scissors[a->scissor_top++] = r;
    set_scissor(a);
}
static void pop_scissor(void *ctx) {
    vv_App *a = ctx;
    if (a->scissor_top > 0) a->scissor_top--;
    set_scissor(a);
}
static const char *clip_get(void *ctx) { (void)ctx; return SDL_GetClipboardText(); }
static void clip_set(void *ctx, const char *s) { (void)ctx; SDL_SetClipboardText(s); }

// ---- app ------------------------------------------------------------------

static void backend_custom(void *ctx, uint32_t id, void *payload, vv_Rect r);

vv_App *vv_app_create(const char *title, int w, int h) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return NULL; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    vv_App *a = calloc(1, sizeof(vv_App));
    a->fs = calloc(1, sizeof(FontSystem));
    a->owns_fs = true;
    a->win = SDL_CreateWindow(title, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!a->win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); free(a); return NULL; }
    a->win_id = SDL_GetWindowID(a->win);
    a->gl = SDL_GL_CreateContext(a->win);
    SDL_GL_MakeCurrent(a->win, a->gl);
    SDL_GL_SetSwapInterval(1);
    SDL_StartTextInput(a->win);   // deliver SDL_EVENT_TEXT_INPUT

    a->rect_prog = link_prog(RECT_VS, RECT_FS);
    a->text_prog = link_prog(TEXT_VS, TEXT_FS);
    a->poly_prog = link_prog(POLY_VS, POLY_FS);
    a->image_prog = link_prog(TEXT_VS, IMAGE_FS); // same vertex layout as text
    a->rect_u_vp = glGetUniformLocation(a->rect_prog, "u_vp");
    a->text_u_vp = glGetUniformLocation(a->text_prog, "u_vp");
    a->text_u_tex = glGetUniformLocation(a->text_prog, "u_tex");
    a->poly_u_vp = glGetUniformLocation(a->poly_prog, "u_vp");
    a->image_u_vp = glGetUniformLocation(a->image_prog, "u_vp");
    a->image_u_tex = glGetUniformLocation(a->image_prog, "u_tex");

    glGenVertexArrays(1, &a->vao);
    glGenBuffers(1, &a->vbo);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    a->backend = (vv_Backend){
        .ctx = a,
        .draw_rects = draw_rects, .draw_text = draw_text,
        .draw_polys = draw_polys, .draw_image = draw_image,
        .texture_create = backend_tex_create, .texture_destroy = backend_tex_destroy,
        .push_scissor = push_scissor, .pop_scissor = pop_scissor,
        .clipboard_get = clip_get, .clipboard_set = clip_set,
        .measure_text = vv_app_measure,
        .custom = backend_custom,
    };
    a->dpi = 1.0f;
    reg_window(a);
    return a;
}

// A second window sharing the parent's GL context: programs, uniform locations,
// the glyph atlas and loaded fonts are shared objects (valid in the new context
// too), so the child only needs its own VAO/VBO and per-window scratch.
vv_App *vv_app_open_child(vv_App *parent, const char *title, int w, int h) {
    if (!parent) return NULL;
    SDL_GL_MakeCurrent(parent->win, parent->gl);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    vv_App *a = calloc(1, sizeof(vv_App));
    a->win = SDL_CreateWindow(title, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!a->win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); free(a); return NULL; }
    a->win_id = SDL_GetWindowID(a->win);
    a->gl = SDL_GL_CreateContext(a->win);
    SDL_GL_MakeCurrent(a->win, a->gl);
    SDL_GL_SetSwapInterval(1);
    SDL_StartTextInput(a->win);

    // Inherit the shared GL objects (valid across the shared context).
    a->rect_prog = parent->rect_prog; a->text_prog = parent->text_prog;
    a->poly_prog = parent->poly_prog; a->image_prog = parent->image_prog;
    a->rect_u_vp = parent->rect_u_vp; a->text_u_vp = parent->text_u_vp;
    a->text_u_tex = parent->text_u_tex; a->poly_u_vp = parent->poly_u_vp;
    a->image_u_vp = parent->image_u_vp; a->image_u_tex = parent->image_u_tex;
    a->fs = parent->fs;   // shared atlas + glyph cache + fonts (GL objects shared)
    a->owns_fs = false;
    a->shares_gl = true;

    // VAOs are not shared between contexts; give the child its own.
    glGenVertexArrays(1, &a->vao);
    glGenBuffers(1, &a->vbo);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    a->backend = (vv_Backend){
        .ctx = a,
        .draw_rects = draw_rects, .draw_text = draw_text,
        .draw_polys = draw_polys, .draw_image = draw_image,
        .texture_create = backend_tex_create, .texture_destroy = backend_tex_destroy,
        .push_scissor = push_scissor, .pop_scissor = pop_scissor,
        .clipboard_get = clip_get, .clipboard_set = clip_set,
        .measure_text = vv_app_measure,
        .custom = backend_custom,
    };
    a->dpi = 1.0f;
    reg_window(a);
    return a;
}

void vv_app_destroy(vv_App *a) {
    if (!a) return;
    unreg_window(a);
    if (a->vec) vv_vector_free(a->vec);
    if (a->clip_cache) SDL_free(a->clip_cache);
    for (int i = 0; i < 8; i++) if (a->cursors[i]) SDL_DestroyCursor(a->cursors[i]);
    if (a->owns_fs && a->fs) {
        for (int i = 0; i < a->fs->font_count; i++) {
            Font *fo = &a->fs->fonts[i];
            if (fo->atlas) glDeleteTextures(1, &fo->atlas);
            if (fo->cache) cr_glyph_cache_free(fo->cache);
            if (fo->font)  cr_font_free(fo->font);
            free(fo->ttf);
        }
        if (a->fs->bake) cr_context_free(a->fs->bake);
        free(a->fs);
    }
    free(a->verts);
    SDL_GL_DestroyContext(a->gl);
    SDL_DestroyWindow(a->win);
    // Only the last window tears SDL down (a child shares the subsystem).
    if (g_nwin == 0) SDL_Quit();
    free(a);
}

vv_Input *vv_app_input(vv_App *a) { return &a->input; }
bool vv_app_should_close(vv_App *a) { return a->should_close; }

void vv_app_wait_event(vv_App *a, int timeout_ms) {
    (void)a;
    // NULL event => wait but leave it queued for the next pump to process.
    SDL_WaitEventTimeout(NULL, timeout_ms);
}

// ---- clipboard + cursor ---------------------------------------------------
static const char *clip_get_cache(void *ud) {
    vv_App *a = ud;
    if (a->clip_cache) SDL_free(a->clip_cache);
    a->clip_cache = SDL_GetClipboardText(); // SDL-owned; freed on the next get
    return a->clip_cache;
}
static void clip_set_direct(void *ud, const char *s) { (void)ud; SDL_SetClipboardText(s); }

void vv_app_bind_clipboard(vv_App *a, vv_Ctx *ctx) {
    vv_set_clipboard_fns(ctx, clip_get_cache, clip_set_direct, a);
}

void vv_app_set_cursor(vv_App *a, vv_CursorShape s) {
    int i = (int)s;
    if (i < 0 || i >= 8) { i = 0; s = VV_CURSOR_DEFAULT; }
    if (!a->cursors[i]) {
        SDL_SystemCursor sys;
        switch (s) {
            case VV_CURSOR_POINTER:   sys = SDL_SYSTEM_CURSOR_POINTER;   break;
            case VV_CURSOR_TEXT:      sys = SDL_SYSTEM_CURSOR_TEXT;      break;
            case VV_CURSOR_RESIZE_H:  sys = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case VV_CURSOR_RESIZE_V:  sys = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            default:                  sys = SDL_SYSTEM_CURSOR_DEFAULT;   break;
        }
        a->cursors[i] = SDL_CreateSystemCursor(sys);
    }
    if (a->cursors[i]) SDL_SetCursor(a->cursors[i]);
}

vv_Backend *vv_app_backend(vv_App *a) { return &a->backend; }

void vv_app_size(vv_App *a, int *w, int *h, float *dpi) {
    SDL_GetWindowSizeInPixels(a->win, &a->fb_w, &a->fb_h);
    a->dpi = SDL_GetWindowDisplayScale(a->win);
    if (a->dpi <= 0) a->dpi = 1.0f;
    if (w) *w = (int)(a->fb_w / a->dpi);
    if (h) *h = (int)(a->fb_h / a->dpi);
    if (dpi) *dpi = a->dpi;
}

static vv_Key map_key(SDL_Keycode k) {
    switch (k) {
        case SDLK_LEFT:      return VV_KEY_LEFT;
        case SDLK_RIGHT:     return VV_KEY_RIGHT;
        case SDLK_UP:        return VV_KEY_UP;
        case SDLK_DOWN:      return VV_KEY_DOWN;
        case SDLK_HOME:      return VV_KEY_HOME;
        case SDLK_END:       return VV_KEY_END;
        case SDLK_BACKSPACE: return VV_KEY_BACKSPACE;
        case SDLK_DELETE:    return VV_KEY_DELETE;
        case SDLK_RETURN:    return VV_KEY_ENTER;
        case SDLK_TAB:       return VV_KEY_TAB;
        case SDLK_ESCAPE:    return VV_KEY_ESCAPE;
        case SDLK_A:         return VV_KEY_A;
        case SDLK_C:         return VV_KEY_C;
        case SDLK_V:         return VV_KEY_V;
        case SDLK_X:         return VV_KEY_X;
        case SDLK_Z:         return VV_KEY_Z;
        default:             return VV_KEY_NONE;
    }
}

// ---- optional framebuffer capture (VV_SHOT=path) -------------------------
// A dependency-free PNG writer (stored/deflate blocks) so screenshots need no
// image library. Used only when VV_SHOT is set — a dev/CI convenience.
static uint32_t vv__crc32(const unsigned char *p, size_t n, uint32_t crc) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int)(crc & 1)));
    }
    return ~crc;
}
static void vv__put32(unsigned char *b, uint32_t v) { b[0]=v>>24; b[1]=v>>16; b[2]=v>>8; b[3]=v; }

static void vv__png_chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
    unsigned char hdr[8]; vv__put32(hdr, len); memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    // CRC covers type + data.
    unsigned char *tmp = malloc(4 + len); memcpy(tmp, type, 4); if (len) memcpy(tmp + 4, data, len);
    uint32_t crc = vv__crc32(tmp, 4 + len, 0); free(tmp);
    unsigned char cb[4]; vv__put32(cb, crc); fwrite(cb, 1, 4, f);
}

// rgba is bottom-up (GL order); write it flipped to a top-down PNG.
static void vv__write_png(const char *path, int w, int h, const unsigned char *rgba) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ihdr[13];
    vv__put32(ihdr, (uint32_t)w); vv__put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0; // 8-bit RGBA
    vv__png_chunk(f, "IHDR", ihdr, 13);

    size_t stride = (size_t)w * 4, raw_len = (stride + 1) * (size_t)h;
    unsigned char *raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        raw[y * (stride + 1)] = 0; // filter: none
        memcpy(raw + y * (stride + 1) + 1, rgba + (size_t)(h - 1 - y) * stride, stride);
    }
    // zlib stream: header + stored deflate blocks + adler32
    size_t zcap = raw_len + raw_len / 65535 * 5 + 32;
    unsigned char *z = malloc(zcap); size_t zn = 0;
    z[zn++] = 0x78; z[zn++] = 0x01;
    size_t off = 0; uint32_t s1 = 1, s2 = 0;
    while (off < raw_len) {
        size_t blk = raw_len - off; if (blk > 65535) blk = 65535;
        z[zn++] = (off + blk >= raw_len) ? 1 : 0;
        z[zn++] = blk & 0xFF; z[zn++] = (blk >> 8) & 0xFF;
        z[zn++] = ~blk & 0xFF; z[zn++] = (~blk >> 8) & 0xFF;
        for (size_t i = 0; i < blk; i++) { z[zn++] = raw[off + i]; s1 = (s1 + raw[off + i]) % 65521; s2 = (s2 + s1) % 65521; }
        off += blk;
    }
    unsigned char adl[4]; vv__put32(adl, (s2 << 16) | s1);
    memcpy(z + zn, adl, 4); zn += 4;
    vv__png_chunk(f, "IDAT", z, (uint32_t)zn);
    vv__png_chunk(f, "IEND", NULL, 0);
    free(z); free(raw); fclose(f);
}

static Uint64 vv__shot_t0 = 0;
static bool   vv__shot_done = false;

static void vv__capture(vv_App *a, const char *path) {
    vv_app_size(a, NULL, NULL, NULL);
    int w = a->fb_w, h = a->fb_h;
    unsigned char *px = malloc((size_t)w * h * 4);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    vv__write_png(path, w, h, px);
    free(px);
    fprintf(stderr, "[vv] wrote %s (%dx%d)\n", path, w, h);
}

bool vv_app_pump(vv_App *a, vv_Input *in) {
    (void)a;
    // Screenshot mode: quit once frame_end has captured the settled frame.
    if (getenv("VV_SHOT") && vv__shot_done) return false;
    SDL_Event e;
    in->wheel = 0;
    in->text_len = 0; in->text[0] = 0;
    in->preedit_len = 0; in->preedit[0] = 0;
    in->key_count = 0;
    SDL_Keymod mod = SDL_GetModState();
    in->shift = (mod & SDL_KMOD_SHIFT) != 0;
    in->ctrl  = (mod & SDL_KMOD_CTRL) != 0;
    in->alt   = (mod & SDL_KMOD_ALT) != 0;

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT: return false;
            case SDL_EVENT_MOUSE_MOTION:
                in->mouse = vv_v2(e.motion.x, e.motion.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) in->mouse_down = true;
                else if (e.button.button == SDL_BUTTON_RIGHT) in->right_down = true; break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) in->mouse_down = false;
                else if (e.button.button == SDL_BUTTON_RIGHT) in->right_down = false; break;
            case SDL_EVENT_MOUSE_WHEEL:
                in->wheel += e.wheel.y; break;
            case SDL_EVENT_TEXT_INPUT: {
                for (const char *p = e.text.text; *p && in->text_len < VV_INPUT_TEXT_CAP - 1; p++)
                    in->text[in->text_len++] = *p;
                in->text[in->text_len] = 0;
                break;
            }
            case SDL_EVENT_TEXT_EDITING: { // IME composition (preedit, uncommitted)
                for (const char *p = e.edit.text; p && *p && in->preedit_len < (int)sizeof in->preedit - 1; p++)
                    in->preedit[in->preedit_len++] = *p;
                in->preedit[in->preedit_len] = 0;
                break;
            }
            case SDL_EVENT_KEY_DOWN: {
                vv_Key k = map_key(e.key.key);
                if (k != VV_KEY_NONE && in->key_count < VV_INPUT_KEY_CAP) {
                    bool sh = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                    bool ct = (e.key.mod & SDL_KMOD_CTRL) != 0;
                    in->keys[in->key_count++] = (vv_KeyEvent){ (uint16_t)k, sh, ct };
                }
                break;
            }
        }
    }
    return true;
}

// ---- multi-window pump ----------------------------------------------------
// Reset the per-frame deltas on a window's input, keeping sticky state (mouse
// position, button-held). Modifiers are global to the OS, so read once.
static void input_begin_frame(vv_Input *in) {
    in->wheel = 0;
    in->text_len = 0; in->text[0] = 0;
    in->preedit_len = 0; in->preedit[0] = 0;
    in->key_count = 0;
    SDL_Keymod mod = SDL_GetModState();
    in->shift = (mod & SDL_KMOD_SHIFT) != 0;
    in->ctrl  = (mod & SDL_KMOD_CTRL) != 0;
    in->alt   = (mod & SDL_KMOD_ALT) != 0;
}

int vv_app_pump_all(void) {
    for (int i = 0; i < g_nwin; i++) input_begin_frame(&g_wins[i]->input);

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        // Route by the window ID carried on each event's sub-struct.
        SDL_WindowID id = 0;
        switch (e.type) {
            case SDL_EVENT_QUIT:
                for (int i = 0; i < g_nwin; i++) g_wins[i]->should_close = true;
                continue;
            case SDL_EVENT_MOUSE_MOTION:      id = e.motion.windowID; break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:   id = e.button.windowID; break;
            case SDL_EVENT_MOUSE_WHEEL:       id = e.wheel.windowID;  break;
            case SDL_EVENT_TEXT_INPUT:        id = e.text.windowID;   break;
            case SDL_EVENT_TEXT_EDITING:      id = e.edit.windowID;   break;
            case SDL_EVENT_KEY_DOWN:          id = e.key.windowID;    break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: id = e.window.windowID; break;
            default: continue;
        }
        vv_App *a = win_by_id(id);
        if (!a) continue;
        vv_Input *in = &a->input;
        switch (e.type) {
            case SDL_EVENT_MOUSE_MOTION:
                in->mouse = vv_v2(e.motion.x, e.motion.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) in->mouse_down = true;
                else if (e.button.button == SDL_BUTTON_RIGHT) in->right_down = true; break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) in->mouse_down = false;
                else if (e.button.button == SDL_BUTTON_RIGHT) in->right_down = false; break;
            case SDL_EVENT_MOUSE_WHEEL:
                in->wheel += e.wheel.y; break;
            case SDL_EVENT_TEXT_INPUT:
                for (const char *p = e.text.text; *p && in->text_len < VV_INPUT_TEXT_CAP - 1; p++)
                    in->text[in->text_len++] = *p;
                in->text[in->text_len] = 0;
                break;
            case SDL_EVENT_TEXT_EDITING:
                for (const char *p = e.edit.text; p && *p && in->preedit_len < (int)sizeof in->preedit - 1; p++)
                    in->preedit[in->preedit_len++] = *p;
                in->preedit[in->preedit_len] = 0;
                break;
            case SDL_EVENT_KEY_DOWN: {
                vv_Key k = map_key(e.key.key);
                if (k != VV_KEY_NONE && in->key_count < VV_INPUT_KEY_CAP)
                    in->keys[in->key_count++] = (vv_KeyEvent){
                        (uint16_t)k, (e.key.mod & SDL_KMOD_SHIFT) != 0,
                        (e.key.mod & SDL_KMOD_CTRL) != 0 };
                break;
            }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                a->should_close = true; break;
        }
    }
    int open = 0;
    for (int i = 0; i < g_nwin; i++) if (!g_wins[i]->should_close) open++;
    return open;
}

// ---- native file dialogs --------------------------------------------------
// SDL invokes the dialog callback later, on the event thread (during a pump).
// We trampoline through a heap record to expose a simple single-path callback.
typedef struct { vv_FileCb cb; void *ud; } DialogTramp;

static void dialog_trampoline(void *ud, const char *const *files, int filter) {
    (void)filter;
    DialogTramp *t = ud;
    const char *path = (files && files[0]) ? files[0] : NULL; // NULL = cancelled
    if (t->cb) t->cb(t->ud, path);
    free(t);
}

void vv_app_open_file(vv_App *a, const char *filter_name, const char *filter_pat,
                      vv_FileCb cb, void *ud) {
    DialogTramp *t = malloc(sizeof *t);
    t->cb = cb; t->ud = ud;
    SDL_DialogFileFilter filt = { filter_name, filter_pat };
    SDL_ShowOpenFileDialog(dialog_trampoline, t, a->win,
                           filter_name ? &filt : NULL, filter_name ? 1 : 0,
                           NULL, false);
}

void vv_app_save_file(vv_App *a, const char *filter_name, const char *filter_pat,
                      const char *default_name, vv_FileCb cb, void *ud) {
    DialogTramp *t = malloc(sizeof *t);
    t->cb = cb; t->ud = ud;
    SDL_DialogFileFilter filt = { filter_name, filter_pat };
    SDL_ShowSaveFileDialog(dialog_trampoline, t, a->win,
                           filter_name ? &filt : NULL, filter_name ? 1 : 0,
                           default_name);
}

// Custom-draw dispatch (§14.3): scissor+viewport to the node's rect (in
// framebuffer pixels, y-flipped) and invoke the app callback, then restore the
// GL state the UI renderer relies on. The app can do arbitrary GL in `fn`.
static void backend_custom(void *ctx, uint32_t id, void *payload, vv_Rect r) {
    (void)id;
    vv_App *a = ctx;
    const vv_CustomDraw *cb = payload;
    if (!cb || !cb->fn) return;
    float s = a->dpi;
    int x = (int)(r.x * s), w = (int)(r.w * s), h = (int)(r.h * s);
    int y = a->fb_h - (int)((r.y + r.h) * s);

    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST); glScissor(x, y, w, h);
    cb->fn(cb->ud, r);
    // Restore what the UI batches assume (they rebind program/VAO/attribs).
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

// Lazily create the craz-backed vector services (icons / SVG / canvas) for this
// app. Shares the app's backend for texture upload; freed in vv_app_destroy.
vv_Vector *vv_app_vector(vv_App *a) {
    if (!a->vec) {
        SDL_GL_MakeCurrent(a->win, a->gl);
        a->vec = vv_vector_new(&a->backend);
    }
    return a->vec;
}

void vv_app_frame_begin(vv_App *a, vv_Color clear) {
    SDL_GL_MakeCurrent(a->win, a->gl);
    if (a->vec) vv_vector_begin_frame(a->vec); // rewind the per-frame ImageRef ring
    vv_app_size(a, NULL, NULL, NULL);
    glViewport(0, 0, a->fb_w, a->fb_h);
    a->scissor_top = 0;
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void vv_app_frame_end(vv_App *a) {
    // Capture the freshly-rendered back buffer after enough frames that the enter
    // animation has settled, then signal quit via the pump. Runs only under
    // VV_SHOT. Counting frames (not wall-clock) survives idle mode stopping the
    // render loop before a time threshold would be hit.
    const char *shot = getenv("VV_SHOT");
    if (shot && !vv__shot_done && ++vv__shot_t0 >= 90) {
        vv__capture(a, shot);
        vv__shot_done = true;
    }
    SDL_GL_SwapWindow(a->win);
}

// ---- turn-key app runner (see vv_sdl_gl.h) ---------------------------------
const char *const *vv_default_font_paths(void) {
    static const char *const fonts[] = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL,
    };
    return fonts;
}

int vv_app_run(const vv_AppDesc *d) {
    if (!d || !d->update || !d->view) return 2;
    int w = d->width  > 0 ? d->width  : 900;
    int h = d->height > 0 ? d->height : 640;
    vv_App *app = vv_app_create(d->title ? d->title : "Verve", w, h);
    if (!app) return 1;

    // An explicit list is loaded in full, so faces land on ids 0,1,2,… (e.g.
    // regular/bold/italic). The default fallback list is just alternate paths
    // for one regular font, so it stops at the first that loads.
    if (d->fonts) {
        for (int i = 0; d->fonts[i]; i++) vv_app_load_font(app, d->fonts[i]);
    } else {
        const char *const *fonts = vv_default_font_paths();
        for (int i = 0; fonts[i]; i++)
            if (vv_app_load_font(app, fonts[i])) break;
    }

    vv_Ctx ctx;
    vv_init(&ctx);
    vv_set_measure_fn(&ctx, vv_app_measure, app);
    // Idle mode stops rendering once the UI settles — but VV_SHOT counts
    // rendered frames to know when the enter animation is done, so keep the
    // render loop running continuously while capturing. A `tick` app animates
    // every frame, so it opts out of idle too.
    vv_set_idle_mode(&ctx, getenv("VV_SHOT") == NULL && d->tick == NULL);
    if (d->clipboard) vv_app_bind_clipboard(app, &ctx);

    // Optional built-in devtools: a node inspector (F12) and perf HUD (F11),
    // attached as overlays over the app. Both start closed so they cost nothing
    // until summoned; when open they route the pointer away from the app.
    vv_Inspector ins; vv_PerfHud hud;
    bool f12_prev = false, f11_prev = false;
    if (d->devtools) {
        vv_inspect_init(&ins, &ctx, vv_app_measure, app);   ins.open = false;
        vv_perf_hud_init(&hud, &ctx, vv_app_measure, app);  hud.open = false;
        // Start an overlay open via env, e.g. VV_DEVTOOLS=perf|inspect|all —
        // handy for screenshots and "always-on" debugging sessions.
        const char *dv = getenv("VV_DEVTOOLS");
        if (dv) {
            ins.open = strstr(dv, "inspect") || strstr(dv, "all");
            hud.open = strstr(dv, "perf")    || strstr(dv, "all");
        }
    }

    vv_Color clear = d->clear.a > 0.0f ? d->clear : vv_rgb(0.11f, 0.12f, 0.14f);
    vv_Input in = {0};
    uint64_t prev = SDL_GetPerformanceCounter();
    while (vv_app_pump(app, &in)) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        int ww, hh; float dpi;
        vv_app_size(app, &ww, &hh, &dpi);
        vv_set_window(&ctx, (float)ww, (float)hh, dpi);

        if (d->tick) { d->tick(d->state, dt); vv_invalidate(&ctx); }

        // Devtools toggles + pointer routing. Splits are pass-through when the
        // overlay is closed, so this is free until F11/F12 is pressed.
        vv_Input app_in = in;
        vv_CommandBuffer *ov_ins = NULL, *ov_hud = NULL;
        if (d->devtools) {
            const bool *ks = SDL_GetKeyboardState(NULL);
            bool f12 = ks[SDL_SCANCODE_F12], f11 = ks[SDL_SCANCODE_F11];
            if (f12 && !f12_prev) vv_inspect_toggle(&ins);
            if (f11 && !f11_prev) vv_perf_hud_toggle(&hud);
            f12_prev = f12; f11_prev = f11;
            app_in = vv_inspect_split(&ins, app_in, (float)ww, (float)hh);
            app_in = vv_perf_hud_split(&hud, app_in, (float)ww, (float)hh);
        }

        vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &app_in, d->update, d->view, d->state);
        if (d->devtools) {
            ov_ins = vv_inspect_render(&ins, dt, (float)ww, (float)hh, dpi);
            ov_hud = vv_perf_hud_render(&hud, dt, (float)ww, (float)hh, dpi);
        }
        vv_app_set_cursor(app, vv_cursor(&ctx));
        if (cmds || ov_ins || ov_hud) {
            vv_app_frame_begin(app, clear);
            if (cmds)   vv_render(vv_app_backend(app), cmds, ww, hh, dpi);
            if (ov_ins) vv_render(vv_app_backend(app), ov_ins, ww, hh, dpi);
            if (ov_hud) vv_render(vv_app_backend(app), ov_hud, ww, hh, dpi);
            vv_app_frame_end(app);
        } else {
            vv_app_wait_event(app, 16); // idle: sleep instead of busy-spinning
        }
    }
    if (d->devtools) { vv_shutdown(&ins.ctx); vv_shutdown(&hud.ctx); }
    vv_shutdown(&ctx);
    vv_app_destroy(app);
    return 0;
}
