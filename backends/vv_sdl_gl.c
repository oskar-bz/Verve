#include "vv_sdl_gl.h"

#include <SDL3/SDL.h>
#include <epoxy/gl.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// One backend, two shader programs: a rounded-box SDF for rects (fill + border +
// shadow + AA in one pass, §9.1) and a textured-quad shader for glyphs.

#define ATLAS_W 1024
#define ATLAS_H 1024
#define BAKE_PX 48.0f          // atlas rasterization size; scaled per draw
#define FIRST_CHAR 32
#define NUM_CHARS 95           // printable ASCII 32..126
#define MAX_FONTS 8
#define MAX_SCISSORS 64

typedef struct {
    bool          loaded;
    stbtt_bakedchar chars[NUM_CHARS];
    GLuint        tex;
    float         ascent;      // scaled to BAKE_PX
} Font;

struct vv_App {
    SDL_Window   *win;
    SDL_GLContext gl;
    vv_Backend    backend;

    GLuint rect_prog, text_prog;
    GLuint vao, vbo;
    GLint  rect_u_vp, text_u_vp, text_u_tex;

    Font   fonts[MAX_FONTS];
    int    font_count;

    vv_Rect scissors[MAX_SCISSORS];
    int     scissor_top;

    int   fb_w, fb_h;
    float dpi;

    // Dynamic vertex scratch (grows).
    float   *verts;
    size_t   vcap, vcount; // in floats
};

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

// ---- vertex scratch -------------------------------------------------------

static void vpush(vv_App *a, size_t n) {
    if (a->vcount + n > a->vcap) {
        size_t nc = a->vcap ? a->vcap * 2 : 4096;
        while (nc < a->vcount + n) nc *= 2;
        a->verts = realloc(a->verts, nc * sizeof(float));
        a->vcap = nc;
    }
}

// ---- font -----------------------------------------------------------------

static const Font *font_of(vv_App *a, vv_FontID id) {
    if (id < (vv_FontID)a->font_count && a->fonts[id].loaded) return &a->fonts[id];
    return a->font_count > 0 && a->fonts[0].loaded ? &a->fonts[0] : NULL;
}

vv_FontID vv_app_load_font(vv_App *a, const char *path) {
    if (a->font_count >= MAX_FONTS) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "font: cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *ttf = malloc((size_t)sz);
    if (fread(ttf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(ttf); return 0; }
    fclose(f);

    unsigned char *bitmap = calloc(ATLAS_W, ATLAS_H);
    Font *fo = &a->fonts[a->font_count];
    stbtt_BakeFontBitmap(ttf, 0, BAKE_PX, bitmap, ATLAS_W, ATLAS_H,
                         FIRST_CHAR, NUM_CHARS, fo->chars);

    // Ascent for baseline placement, scaled to BAKE_PX.
    stbtt_fontinfo info; stbtt_InitFont(&info, ttf, 0);
    int asc, desc, gap; stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    fo->ascent = (float)asc * stbtt_ScaleForPixelHeight(&info, BAKE_PX);

    glGenTextures(1, &fo->tex);
    glBindTexture(GL_TEXTURE_2D, fo->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(bitmap); free(ttf);
    fo->loaded = true;
    return (vv_FontID)a->font_count++;
}

// Shared glyph walk: measures (emit==NULL) or emits quads. Single line; the
// layout engine handles multi-line via wrap width in a later phase — for now we
// wrap greedily on spaces to keep measure and draw in agreement.
static vv_Vec2 text_layout(vv_App *a, const Font *fo, const char *s, int len,
                           float size, float wrap, float ox, float oy,
                           vv_Color col, bool emit) {
    if (!fo) return vv_v2(0, 0);
    float scale = size / BAKE_PX;
    float line_h = size * 1.25f;
    float x = 0, y = 0, maxw = 0;
    int i = 0;
    while (i < len) {
        // Measure the next word to decide on a wrap break.
        int ws = i; while (ws < len && s[ws] != ' ' && s[ws] != '\n') ws++;
        float word_w = 0;
        for (int k = i; k < ws; k++) {
            int c = (unsigned char)s[k];
            if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) continue;
            word_w += fo->chars[c - FIRST_CHAR].xadvance * scale;
        }
        if (wrap > 0 && x > 0 && x + word_w > wrap) { x = 0; y += line_h; }

        for (int k = i; k < ws; k++) {
            int c = (unsigned char)s[k];
            if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) continue;
            stbtt_aligned_quad q;
            float bx = x / scale, by = fo->ascent; // bake-space cursor
            stbtt_GetBakedQuad(fo->chars, ATLAS_W, ATLAS_H, c - FIRST_CHAR, &bx, &by, &q, 1);
            x = bx * scale;
            if (emit) {
                // q coords are in bake space relative to baseline; scale + place.
                float x0 = ox + q.x0 * scale, x1 = ox + q.x1 * scale;
                float y0 = oy + y + (q.y0) * scale + (line_h - size) * 0.0f;
                float y1 = oy + y + (q.y1) * scale;
                float verts[6][8] = {
                    {x0,y0, q.s0,q.t0, col.r,col.g,col.b,col.a},
                    {x1,y0, q.s1,q.t0, col.r,col.g,col.b,col.a},
                    {x1,y1, q.s1,q.t1, col.r,col.g,col.b,col.a},
                    {x0,y0, q.s0,q.t0, col.r,col.g,col.b,col.a},
                    {x1,y1, q.s1,q.t1, col.r,col.g,col.b,col.a},
                    {x0,y1, q.s0,q.t1, col.r,col.g,col.b,col.a},
                };
                vpush(a, 6 * 8);
                memcpy(a->verts + a->vcount, verts, sizeof verts);
                a->vcount += 6 * 8;
            }
        }
        maxw = fmaxf(maxw, x);
        if (ws < len && s[ws] == '\n') { x = 0; y += line_h; i = ws + 1; continue; }
        if (ws < len) { // space
            int c = ' ';
            x += fo->chars[c - FIRST_CHAR].xadvance * scale;
            i = ws + 1;
        } else i = ws;
    }
    return vv_v2(fmaxf(maxw, x), y + line_h);
}

vv_Vec2 vv_app_measure(void *ud, const char *s, int len, vv_FontID font,
                       float size, float wrap_width) {
    vv_App *a = ud;
    return text_layout(a, font_of(a, font), s, len, size, wrap_width, 0, 0,
                       (vv_Color){0}, false);
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
    const Font *fo = font_of(a, n > 0 ? runs[0].font : 0);
    if (!fo) return;
    a->vcount = 0;
    for (int i = 0; i < n; i++) {
        const vv_CmdText *t = &runs[i];
        // origin.y is the baseline; our text_layout places from top, so shift up
        // by ascent to align the baseline.
        float top = t->origin.y - fo->ascent * (t->size / BAKE_PX);
        text_layout(a, fo, t->utf8, (int)t->len, t->size, 0,
                    t->origin.x, top, t->color, true);
    }
    if (a->vcount == 0) return;

    glUseProgram(a->text_prog);
    glUniform2f(a->text_u_vp, (float)a->fb_w / a->dpi, (float)a->fb_h / a->dpi);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fo->tex);
    glUniform1i(a->text_u_tex, 0);
    glBindVertexArray(a->vao);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(a->vcount * sizeof(float)), a->verts, GL_STREAM_DRAW);
    GLsizei stride = 8 * sizeof(float);
    for (int i = 0; i < 3; i++) glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(a->vcount / 8));
}

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

vv_App *vv_app_create(const char *title, int w, int h) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return NULL; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    vv_App *a = calloc(1, sizeof(vv_App));
    a->win = SDL_CreateWindow(title, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!a->win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); free(a); return NULL; }
    a->gl = SDL_GL_CreateContext(a->win);
    SDL_GL_MakeCurrent(a->win, a->gl);
    SDL_GL_SetSwapInterval(1);
    SDL_StartTextInput(a->win);   // deliver SDL_EVENT_TEXT_INPUT

    a->rect_prog = link_prog(RECT_VS, RECT_FS);
    a->text_prog = link_prog(TEXT_VS, TEXT_FS);
    a->rect_u_vp = glGetUniformLocation(a->rect_prog, "u_vp");
    a->text_u_vp = glGetUniformLocation(a->text_prog, "u_vp");
    a->text_u_tex = glGetUniformLocation(a->text_prog, "u_tex");

    glGenVertexArrays(1, &a->vao);
    glGenBuffers(1, &a->vbo);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    a->backend = (vv_Backend){
        .ctx = a,
        .draw_rects = draw_rects, .draw_text = draw_text,
        .push_scissor = push_scissor, .pop_scissor = pop_scissor,
        .clipboard_get = clip_get, .clipboard_set = clip_set,
        .measure_text = vv_app_measure,
    };
    a->dpi = 1.0f;
    return a;
}

void vv_app_destroy(vv_App *a) {
    if (!a) return;
    free(a->verts);
    SDL_GL_DestroyContext(a->gl);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    free(a);
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

bool vv_app_pump(vv_App *a, vv_Input *in) {
    (void)a; // single-window: SDL events aren't window-filtered yet
    SDL_Event e;
    in->wheel = 0;
    in->text_len = 0; in->text[0] = 0;
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
                if (e.button.button == SDL_BUTTON_LEFT) in->mouse_down = true; break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) in->mouse_down = false; break;
            case SDL_EVENT_MOUSE_WHEEL:
                in->wheel += e.wheel.y; break;
            case SDL_EVENT_TEXT_INPUT: {
                for (const char *p = e.text.text; *p && in->text_len < VV_INPUT_TEXT_CAP - 1; p++)
                    in->text[in->text_len++] = *p;
                in->text[in->text_len] = 0;
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

void vv_app_frame_begin(vv_App *a, vv_Color clear) {
    SDL_GL_MakeCurrent(a->win, a->gl);
    vv_app_size(a, NULL, NULL, NULL);
    glViewport(0, 0, a->fb_w, a->fb_h);
    a->scissor_top = 0;
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void vv_app_frame_end(vv_App *a) { SDL_GL_SwapWindow(a->win); }
