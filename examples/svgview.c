// svgview.c — a real-time SVG viewer built on the Verve UI + the craz CPU
// vector rasterizer (../craz). It parses an SVG, rasterizes it on the CPU each
// frame with a configurable number of worker threads, uploads the result as a
// texture, and shows it in a pan/zoomable canvas alongside live timing metrics.
//
// Extended with a font/atlas debug panel: load a TTF, render text through the
// craz glyph cache, and visualize the skyline rectangle packing + glyph atlas
// in real-time — useful for debugging the atlas packer.
//
//   Build:  make svgview   ->   ./build/svgview [file.svg]
//
// Interaction (over the canvas):
//   • scroll wheel  — zoom toward the cursor
//   • left drag     — pan
//   • the sidebar   — pick a sample, set thread count, toggle continuous render
//
// Why a manual loop (not vv_app_run): we need the backend handle every frame to
// (re)upload the rasterized texture, and we want to control the exact order of
// "handle input -> rasterize -> build view -> present". So this drives the
// begin/build/end + present loop itself and drains widget events by hand.
#define _POSIX_C_SOURCE 200112L
#include "vv_sdl_gl.h"
#include "craz/svg.h"
#include "craz/font.h"
#include "craz/bake.h"

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <ctype.h>

// ---------------------------------------------------------------- thread pool
// A persistent pool of exactly N workers implementing craz's cr_dispatch_fn.
// Persistent (not spawn-per-frame) so the thread-count metric reflects raster
// cost, not pthread_create overhead. Workers claim band indices atomically; the
// calling (main) thread blocks until the job's items all complete.
typedef struct {
  pthread_t *threads;
  int n;
  pthread_mutex_t mtx;
  pthread_cond_t work_cv, done_cv;
  void (*fn)(void *, int);
  void *fn_ctx;
  int count;
  atomic_int next;
  int active;    // workers still running the current job
  uint64_t gen;  // job generation; bumped to release the workers
  bool stop;
} Pool;

static void *pool_worker(void *arg) {
  Pool *p = arg;
  pthread_mutex_lock(&p->mtx);
  uint64_t seen = p->gen;
  for (;;) {
    while (!p->stop && p->gen == seen)
      pthread_cond_wait(&p->work_cv, &p->mtx);
    if (p->stop) {
      pthread_mutex_unlock(&p->mtx);
      return NULL;
    }
    seen = p->gen;
    void (*fn)(void *, int) = p->fn;
    void *fc = p->fn_ctx;
    int count = p->count;
    pthread_mutex_unlock(&p->mtx);

    int i;
    while ((i = atomic_fetch_add(&p->next, 1)) < count)
      fn(fc, i);

    pthread_mutex_lock(&p->mtx);
    if (--p->active == 0)
      pthread_cond_signal(&p->done_cv);
  }
}

// cr_dispatch_fn: run fn(fn_ctx, i) for every i in [0,count) across the workers.
static void pool_dispatch(void *user, void (*fn)(void *, int), void *fn_ctx,
                          int count) {
  Pool *p = user;
  pthread_mutex_lock(&p->mtx);
  p->fn = fn;
  p->fn_ctx = fn_ctx;
  p->count = count;
  atomic_store(&p->next, 0);
  p->active = p->n;
  p->gen++;
  pthread_cond_broadcast(&p->work_cv);
  while (p->active != 0)
    pthread_cond_wait(&p->done_cv, &p->mtx);
  pthread_mutex_unlock(&p->mtx);
}

static Pool *pool_create(int n) {
  Pool *p = calloc(1, sizeof *p);
  p->n = n;
  pthread_mutex_init(&p->mtx, NULL);
  pthread_cond_init(&p->work_cv, NULL);
  pthread_cond_init(&p->done_cv, NULL);
  atomic_init(&p->next, 0);
  p->threads = calloc((size_t)n, sizeof(pthread_t));
  for (int i = 0; i < n; i++)
    pthread_create(&p->threads[i], NULL, pool_worker, p);
  return p;
}

static void pool_destroy(Pool *p) {
  if (!p)
    return;
  pthread_mutex_lock(&p->mtx);
  p->stop = true;
  p->gen++;
  pthread_cond_broadcast(&p->work_cv);
  pthread_mutex_unlock(&p->mtx);
  for (int i = 0; i < p->n; i++)
    pthread_join(p->threads[i], NULL);
  pthread_mutex_destroy(&p->mtx);
  pthread_cond_destroy(&p->work_cv);
  pthread_cond_destroy(&p->done_cv);
  free(p->threads);
  free(p);
}

// ---------------------------------------------------------------------- state
enum {
  MSG_OPEN = 1, // open the OS file picker
  MSG_SAMPLE,   // load sample svg (payload as_int = index into SAMPLES)
  MSG_THREADS,  // thread-count slider (payload as_float)
  MSG_CONTINUOUS,
  MSG_RESET, // reset pan/zoom to fit
  MSG_DEBUG_TEXT,   // debug text input changed
  MSG_DEBUG_TOGGLE, // toggle debug panel
  MSG_FONT_SIZE,   // font size slider
};

typedef struct {
  const char *label;
  const char *path;
} Sample;

// Sample documents shipped with craz. Paths are relative to ../craz.
static const Sample SAMPLES[] = {
    {"Tiger", "../craz/svgs/tiger.svg"},
    {"Boston map", "../craz/svgs/boston.svg"},
    {"Periodic table", "../craz/svgs/periodic-table.svg"},
    {"Paris (30k paths)", "../craz/svgs/paris-30k.svg"},
    {"Paragraphs", "../craz/svgs/paragraphs.svg"},
};
enum { NSAMPLES = (int)(sizeof SAMPLES / sizeof SAMPLES[0]) };

#define METRIC_HISTORY 180
#define SIDEBAR_W 300.0f
#define ATLAS_ZOOM 3.0f        // zoom factor for the atlas preview
#define ATLAS_PREVIEW_H 240.0f  // max height of atlas preview in the sidebar
#define DEBUG_TEXT_CAP 256

typedef struct {
  vv_App *app;

  // document
  cr_svg *doc;
  char path[512];
  char err[256];
  float svg_w, svg_h;

  // view transform, in canvas device pixels (pan) + scalar zoom
  float zoom;
  float pan_x, pan_y;
  int selected_sample; // -1 if none

  // rasterization
  int nthreads, max_threads;
  bool continuous;
  cr_context **ctxs; // one per band; count == cfg_threads
  int cfg_threads;   // thread count the ctxs/pool are currently built for
  Pool *pool;        // NULL when cfg_threads <= 1 (serial)

  // canvas backing store + texture
  uint8_t *buf;
  int buf_w, buf_h;
  vv_TexID tex;
  vv_ImageRef img;
  bool have_tex;

  // metrics
  bool dirty;
  double last_ms;
  float ms_hist[METRIC_HISTORY];
  int ms_count;

  // input tracking
  bool dragging;
  vv_Vec2 drag_last;

  // -------------------------------------------------------- font debug state
  cr_font *font;
  cr_glyph_cache *glyph_cache;
  cr_context *bake_ctx;  // single context for baking glyphs
  float font_size;
  bool debug_open;       // debug panel visibility
  char debug_text[DEBUG_TEXT_CAP];

  // atlas display texture (converted A8->RGBA)
  uint8_t *atlas_rgba;
  int atlas_rgba_w, atlas_rgba_h;
  vv_TexID atlas_tex;
  vv_ImageRef atlas_img;
  bool atlas_tex_valid;

  // cached debug info from last frame
  int cache_entry_count;
  float atlas_fill_ratio;
} App;

// ------------------------------------------------------------- raster helpers
static double now_ms(void) {
  return (double)SDL_GetPerformanceCounter() * 1000.0 /
         (double)SDL_GetPerformanceFrequency();
}

// Rebuild the per-band contexts (and the pool) for `n` threads.
static void reconfigure_threads(App *a, int n) {
  if (n < 1)
    n = 1;
  if (a->ctxs) {
    for (int i = 0; i < a->cfg_threads; i++)
      cr_context_free(a->ctxs[i]);
    free(a->ctxs);
  }
  pool_destroy(a->pool);
  a->pool = NULL;

  a->cfg_threads = n;
  a->ctxs = calloc((size_t)n, sizeof(cr_context *));
  for (int i = 0; i < n; i++)
    a->ctxs[i] = cr_context_new();
  if (n > 1)
    a->pool = pool_create(n);
}

// Paint a checkerboard into the canvas buffer so a transparent SVG reads
// clearly. Opaque (a=255), so the uploaded texture needs no alpha compositing.
static void fill_checker(uint8_t *buf, int w, int h) {
  const int cell = 12;
  for (int y = 0; y < h; y++) {
    uint8_t *row = buf + (size_t)y * w * 4;
    int yc = (y / cell) & 1;
    for (int x = 0; x < w; x++) {
      uint8_t v = ((x / cell) & 1) ^ yc ? 46 : 38;
      row[x * 4 + 0] = v;
      row[x * 4 + 1] = v + 2;
      row[x * 4 + 2] = v + 5;
      row[x * 4 + 3] = 255;
    }
  }
}

static void load_svg(App *a, const char *path) {
  cr_svg *doc = cr_svg_parse_file(path);
  if (!doc) {
    snprintf(a->err, sizeof a->err, "failed to parse %s", path);
    return;
  }
  if (a->doc)
    cr_svg_free(a->doc);
  a->doc = doc;
  a->err[0] = 0;
  snprintf(a->path, sizeof a->path, "%s", path);
  a->svg_w = cr_svg_width(doc);
  a->svg_h = cr_svg_height(doc);
  a->zoom = 1.0f; // reset to fit
  a->pan_x = a->pan_y = 0.0f;
  a->dirty = true;
}

// Rasterize the document into `buf` (sized cw x ch, device pixels) with the
// current pan/zoom, timing the raster call. Returns wall-clock milliseconds.
static double rasterize(App *a, int cw, int ch) {
  cr_surface s = {a->buf, cw, ch, cw * 4, CR_FORMAT_RGBA8_PREMUL};
  fill_checker(a->buf, cw, ch);
  if (!a->doc)
    return 0.0;

  // user units -> device: fit-to-canvas, then apply zoom and pan (device px).
  cr_matrix fit = cr_svg_fit(a->doc, (float)cw, (float)ch);
  cr_matrix m = cr_matrix_mul(fit, cr_matrix_scale(a->zoom, a->zoom));
  m = cr_matrix_mul(m, cr_matrix_translate(a->pan_x, a->pan_y));

  double t0 = now_ms();
  if (a->cfg_threads <= 1 || !a->pool) {
    cr_svg_render(a->doc, a->ctxs[0], &s, &m);
  } else {
    cr_svg_render_tiled(a->doc, (cr_context *const *)a->ctxs, a->cfg_threads, &s,
                        &m, pool_dispatch, a->pool);
  }
  return now_ms() - t0;
}

// Re-raster (resizing the buffer to the canvas) and re-upload the texture.
static void update_canvas(App *a, int cw, int ch) {
  if (cw < 1)
    cw = 1;
  if (ch < 1)
    ch = 1;
  if (cw != a->buf_w || ch != a->buf_h) {
    free(a->buf);
    a->buf = malloc((size_t)cw * ch * 4);
    a->buf_w = cw;
    a->buf_h = ch;
    a->dirty = true;
  }
  if (a->cfg_threads != a->nthreads)
    reconfigure_threads(a, a->nthreads);

  double ms = rasterize(a, cw, ch);
  a->last_ms = ms;
  if (a->ms_count < METRIC_HISTORY) {
    a->ms_hist[a->ms_count++] = (float)ms;
  } else {
    memmove(a->ms_hist, a->ms_hist + 1, sizeof(float) * (METRIC_HISTORY - 1));
    a->ms_hist[METRIC_HISTORY - 1] = (float)ms;
  }

  if (a->have_tex)
    vv_app_texture_destroy(a->app, a->tex);
  a->tex = vv_app_texture_from_rgba(a->app, a->buf, cw, ch);
  a->have_tex = true;
  a->img = (vv_ImageRef){.tex = a->tex,
                         .uv = {0, 0, 1, 1},
                         .tint = {1, 1, 1, 1}};
}

// ---------------------------------------------------------- font debug helpers

// Convert A8 coverage to RGBA (white text on a dark background).
static void atlas_a8_to_rgba(const uint8_t *a8, uint8_t *rgba,
                              int w, int h, int a8_stride, int rgba_stride) {
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t v = a8[y * a8_stride + x];
      rgba[y * rgba_stride + x * 4 + 0] = v;
      rgba[y * rgba_stride + x * 4 + 1] = v;
      rgba[y * rgba_stride + x * 4 + 2] = v;
      rgba[y * rgba_stride + x * 4 + 3] = 255;
    }
  }
}

// Render all codepoints in the debug text buffer through the glyph cache.
// This populates the atlas with new glyphs on cache misses.
static void render_debug_text(App *a) {
  if (!a->glyph_cache || !a->bake_ctx || !a->debug_text[0])
    return;
  const char *p = a->debug_text;
  float pen_x = 0.0f;
  float line_height = a->font_size * 1.3f;
  while (*p) {
    int cp = (unsigned char)*p;
    int len = 1;
    if ((cp & 0xe0) == 0xc0) {
      cp = ((int)p[0] & 0x1f) << 6;
      len = 2;
      if (p[1]) cp |= p[1] & 0x3f;
    } else if ((cp & 0xf0) == 0xe0) {
      cp = ((int)p[0] & 0x0f) << 12;
      len = 3;
      if (p[1]) cp |= ((int)p[1] & 0x3f) << 6;
      if (p[2]) cp |= p[2] & 0x3f;
    } else if ((cp & 0xf8) == 0xf0) {
      cp = ((int)p[0] & 0x07) << 18;
      len = 4;
      if (p[1]) cp |= ((int)p[1] & 0x3f) << 12;
      if (p[2]) cp |= ((int)p[2] & 0x3f) << 6;
      if (p[3]) cp |= p[3] & 0x3f;
    }
    if (cp == '\n') {
      pen_x = 0.0f;
      p += len;
      continue;
    }
    cr_glyph_entry entry;
    if (cr_glyph_cache_get(a->glyph_cache, a->bake_ctx, cp, a->font_size, pen_x, &entry)) {
      pen_x += entry.advance;
    } else {
      pen_x += a->font_size * 0.5f;
    }
    p += len;
  }
}

// Update the atlas display texture (convert A8 -> RGBA and upload to GPU).
static void update_atlas_texture(App *a) {
  if (!a->glyph_cache)
    return;
  int aw, ah, stride;
  const uint8_t *a8 = cr_glyph_cache_pixels(a->glyph_cache, &aw, &ah, &stride);
  if (!a8 || aw <= 0 || ah <= 0)
    return;

  if (aw != a->atlas_rgba_w || ah != a->atlas_rgba_h) {
    free(a->atlas_rgba);
    a->atlas_rgba = malloc((size_t)aw * ah * 4);
    a->atlas_rgba_w = aw;
    a->atlas_rgba_h = ah;
  }

  atlas_a8_to_rgba(a8, a->atlas_rgba, aw, ah, stride, aw * 4);

  if (a->atlas_tex_valid)
    vv_app_texture_destroy(a->app, a->atlas_tex);
  a->atlas_tex = vv_app_texture_from_rgba(a->app, a->atlas_rgba, aw, ah);
  a->atlas_tex_valid = true;
  a->atlas_img = (vv_ImageRef){.tex = a->atlas_tex,
                               .uv = {0, 0, 1, 1},
                               .tint = {1, 1, 1, 1}};

  // Compute stats
  cr_atlas_debug ad;
  const cr_atlas *atlas = cr_glyph_cache_get_atlas(a->glyph_cache);
  a->cache_entry_count = 0;
  a->atlas_fill_ratio = 0.0f;
  if (cr_atlas_debug_info(atlas, &ad) && ad.w > 0 && ad.h > 0) {
    int max_y = 0;
    for (int i = 0; i < ad.nnodes; i++)
      if (ad.nodes[i].y > max_y) max_y = ad.nodes[i].y;
    a->atlas_fill_ratio = (float)max_y / (float)ad.h;
    a->cache_entry_count = cr_glyph_cache_enumerate(a->glyph_cache, NULL, NULL);
  }
}

// ------------------------------------------------------------------- update()
static void on_file_picked(void *ud, const char *path) {
  if (path)
    load_svg((App *)ud, path);
}

static void update(void *state, vv_Event ev) {
  App *a = state;
  switch (ev.msg) {
  case MSG_OPEN:
    vv_app_open_file(a->app, "SVG", "svg", on_file_picked, a);
    break;
  case MSG_SAMPLE: {
    int i = (int)ev.data.as_int;
    if (i >= 0 && i < NSAMPLES) {
      a->selected_sample = i;
      load_svg(a, SAMPLES[i].path);
    }
    break;
  }
  case MSG_THREADS: {
    int n = (int)lroundf((float)ev.data.as_float);
    if (n < 1) n = 1;
    if (n > a->max_threads) n = a->max_threads;
    if (n != a->nthreads) { a->nthreads = n; a->dirty = true; }
    break;
  }
  case MSG_CONTINUOUS:
    a->continuous = ev.data.as_int != 0;
    break;
  case MSG_RESET:
    a->zoom = 1.0f; a->pan_x = a->pan_y = 0.0f; a->dirty = true;
    break;
  case MSG_DEBUG_TEXT:
    render_debug_text(a);
    update_atlas_texture(a);
    break;
  case MSG_DEBUG_TOGGLE:
    a->debug_open = ev.data.as_int != 0;
    break;
  case MSG_FONT_SIZE: {
    float sz = (float)ev.data.as_float;
    if (sz < 8.0f) sz = 8.0f;
    if (sz > 256.0f) sz = 256.0f;
    if (sz != a->font_size) {
      a->font_size = sz;
      if (a->glyph_cache) { cr_glyph_cache_free(a->glyph_cache); a->glyph_cache = NULL; }
      if (a->font)
        a->glyph_cache = cr_glyph_cache_new(a->font, 512, 512, 4);
      a->atlas_tex_valid = false;
      render_debug_text(a);
      update_atlas_texture(a);
    }
    break;
  }
  default: break;
  }
}

// Callback for cr_glyph_cache_enumerate: draw a colored outline around each
// glyph's atlas rect, scaled to the preview coordinates.
struct GlyphVisit { vv_Ctx *ctx; uint32_t node; float sx, sy; };
static void draw_glyph_outline(void *ud, const cr_glyph_entry *e) {
  struct GlyphVisit *gv = (struct GlyphVisit *)ud;
  if (e->w <= 0 || e->h <= 0) return;
  float x = (float)e->x * gv->sx;
  float y = (float)e->y * gv->sy;
  float w = (float)e->w * gv->sx;
  float h = (float)e->h * gv->sy;
  vv_Vec2 pts[5] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, {x, y}};
  vv_draw_polyline(gv->ctx, gv->node, pts, 5, 1.0f,
                   (vv_Color){0.2f, 0.8f, 1.0f, 0.7f});
}

// --------------------------------------------------------------------- view()

static void metric_row(vv_Ctx *c, const vv_Theme *t, const char *k,
                       const char *v) {
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                      .main = VV_ALIGN_SPACE_BETWEEN, .cross = VV_ALIGN_CENTER),
         VV_STYLE(.bg = {0})) {
    vv_text(c, k, VV_STYLE(.fg = t->text_secondary, .font_size = 13));
    vv_text(c, v, VV_STYLE(.fg = t->text_primary, .font_size = 13));
  }
}

static void view(vv_Ctx *c, void *state) {
  App *a = state;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = t->surface_app)) {

    // ---- sidebar ----------------------------------------------------------
    VV_BOX(c,
           VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(SIDEBAR_W), .h = vv_grow(1),
                     .padding = vv_all(16), .gap = 12, .scroll_y = true),
           VV_STYLE(.bg = t->surface_panel,
                    .border_width = {0, 0, 1, 0},
                    .border_color = t->border_subtle)) {
      vv_text(c, "craz \xc2\xb7 SVG viewer",
              VV_STYLE(.fg = t->text_primary, .font_size = 20));
      vv_text(c, "CPU vector rasterizer",
              VV_STYLE(.fg = t->text_muted, .font_size = 12));

      vv_button(c, "open", "Open SVG\xe2\x80\xa6", MSG_OPEN, VV_NO_PAYLOAD);

      vv_text(c, "SAMPLES",
              VV_STYLE(.fg = t->text_muted, .font_size = 11));
      for (int i = 0; i < NSAMPLES; i++)
        vv_list_item(c, SAMPLES[i].path, SAMPLES[i].label,
                     a->selected_sample == i, MSG_SAMPLE, vv_pi(i));

      VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
             VV_STYLE(.bg = t->border_subtle)) {}

      // threads
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                          .main = VV_ALIGN_SPACE_BETWEEN),
             VV_STYLE(.bg = {0})) {
        vv_text(c, "Threads", VV_STYLE(.fg = t->text_secondary, .font_size = 13));
        vv_text(c, vv_fmt(c, "%d / %d", a->nthreads, a->max_threads),
                VV_STYLE(.fg = t->brand_primary, .font_size = 13));
      }
      vv_slider(c, "threads", (float)a->nthreads, 1.0f, (float)a->max_threads,
                MSG_THREADS);

      vv_checkbox(c, "cont", "Continuous render", a->continuous, MSG_CONTINUOUS);
      vv_button(c, "reset", "Reset view", MSG_RESET, VV_NO_PAYLOAD);

      VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
             VV_STYLE(.bg = t->border_subtle)) {}

      // metrics
      vv_text(c, "METRICS", VV_STYLE(.fg = t->text_muted, .font_size = 11));

      double px = (double)a->buf_w * (double)a->buf_h;
      double fps = a->last_ms > 0 ? 1000.0 / a->last_ms : 0.0;
      double mpx = a->last_ms > 0 ? px / (a->last_ms / 1000.0) / 1e6 : 0.0;
      double avg = 0;
      for (int i = 0; i < a->ms_count; i++)
        avg += a->ms_hist[i];
      if (a->ms_count)
        avg /= a->ms_count;

      metric_row(c, t, "Canvas", vv_fmt(c, "%d\xc3\x97%d", a->buf_w, a->buf_h));
      if (a->doc)
        metric_row(c, t, "Document",
                   vv_fmt(c, "%.0f\xc3\x97%.0f", a->svg_w, a->svg_h));
      metric_row(c, t, "Zoom", vv_fmt(c, "%.0f%%", a->zoom * 100.0f));
      metric_row(c, t, "Raster", vv_fmt(c, "%.2f ms", a->last_ms));
      metric_row(c, t, "Average", vv_fmt(c, "%.2f ms", avg));
      metric_row(c, t, "Rate", vv_fmt(c, "%.0f fps", fps));
      metric_row(c, t, "Throughput", vv_fmt(c, "%.0f Mpx/s", mpx));

      if (a->ms_count > 1) {
        vv_PlotSeries series = {.ys = a->ms_hist,
                                .count = a->ms_count,
                                .color = t->brand_primary,
                                .kind = VV_PLOT_LINE,
                                .name = "raster ms"};
        vv_plot(c, "hist", &series, 1,
                (vv_PlotOpts){.auto_y = true, .grid = true, .height = 90});
      }

      if (a->err[0])
        vv_text(c, a->err, VV_STYLE(.fg = t->status_error, .font_size = 12));

      // --------------------------------------------------------- debug panel
      VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
             VV_STYLE(.bg = t->border_subtle)) {}

      vv_checkbox(c, "dbg", "Atlas debug", a->debug_open, MSG_DEBUG_TOGGLE);

      if (a->debug_open && a->font) {
        vv_text(c, "DEBUG TEXT",
                VV_STYLE(.fg = t->text_muted, .font_size = 11));
        vv_text_field(c, "dbgtext", a->debug_text, DEBUG_TEXT_CAP,
                      "Type text here...", MSG_DEBUG_TEXT);

        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                            .main = VV_ALIGN_SPACE_BETWEEN),
               VV_STYLE(.bg = {0})) {
          vv_text(c, "Size", VV_STYLE(.fg = t->text_secondary, .font_size = 13));
          vv_text(c, vv_fmt(c, "%.0f", a->font_size),
                  VV_STYLE(.fg = t->brand_primary, .font_size = 13));
        }
        vv_slider(c, "fontsize", a->font_size, 8.0f, 256.0f, MSG_FONT_SIZE);

        VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
               VV_STYLE(.bg = t->border_subtle)) {}
        vv_text(c, "ATLAS STATS",
                VV_STYLE(.fg = t->text_muted, .font_size = 11));

        if (a->atlas_tex_valid) {
          metric_row(c, t, "Size",
                     vv_fmt(c, "%d\xc3\x97%d", a->atlas_rgba_w, a->atlas_rgba_h));
          metric_row(c, t, "Entries", vv_fmt(c, "%d", a->cache_entry_count));
          metric_row(c, t, "Fill",
                     vv_fmt(c, "%.0f%%", a->atlas_fill_ratio * 100.0f));

          // Compute atlas preview size (zoom, capped height)
          float preview_w = (float)a->atlas_rgba_w * ATLAS_ZOOM;
          float preview_h = (float)a->atlas_rgba_h * ATLAS_ZOOM;
          if (preview_h > ATLAS_PREVIEW_H) {
            float scale = ATLAS_PREVIEW_H / preview_h;
            preview_w *= scale;
            preview_h *= scale;
          }

          // Draw atlas texture
          uint32_t img_node = vv_image(c, "atlas", &a->atlas_img,
                                       vv_fixed(preview_w), vv_fixed(preview_h));

          // Draw skyline nodes and glyph rects as overlays on the image
          const cr_atlas *atlas = cr_glyph_cache_get_atlas(a->glyph_cache);
          cr_atlas_debug ad;
          if (cr_atlas_debug_info(atlas, &ad) && ad.nnodes > 0) {
            float sx = preview_w / (float)ad.w;
            float sy = preview_h / (float)ad.h;

            // Skyline nodes: each is a segment at y=(node.y) spanning node.width
            for (int i = 0; i < ad.nnodes; i++) {
              float nx = (float)ad.nodes[i].x * sx;
              float ny = (float)ad.nodes[i].y * sy;
              float nw = (float)ad.nodes[i].width * sx;
              vv_Vec2 pts[4] = {
                {nx, ny}, {nx + nw, ny},
                {nx + nw, ny + 2.0f}, {nx, ny + 2.0f}
              };
              vv_draw_polygon(c, img_node, pts, 4, (vv_Color){1, 0, 0.5f, 0.6f});
            }

            // Enumerate cached glyphs and draw their rects
            struct GlyphVisit { vv_Ctx *ctx; uint32_t node; float sx, sy; };
            struct GlyphVisit gv = { .ctx = c, .node = img_node, .sx = sx, .sy = sy };
            cr_glyph_cache_enumerate(a->glyph_cache, draw_glyph_outline, &gv);
          }
        } else {
          vv_text(c, "(type some text to see the atlas)",
                  VV_STYLE(.fg = t->text_muted, .font_size = 12));
        }
      } else if (a->debug_open && !a->font) {
        vv_text(c, "(no font loaded)",
                VV_STYLE(.fg = t->status_error, .font_size = 12));
      }
    }

    // ---- canvas -----------------------------------------------------------
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                        .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
           VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.13f))) {
      if (a->have_tex) {
        vv_image(c, "canvas", &a->img, vv_grow(1), vv_grow(1));
      } else {
        vv_text(c, "Open an SVG or pick a sample \xe2\x86\x90",
                VV_STYLE(.fg = t->text_muted, .font_size = 16));
      }
    }
  }
}

// ----------------------------------------------------------------- main loop
// Handle wheel-zoom (toward cursor) and drag-pan while the cursor is over the
// canvas. Coordinates are converted from logical UI space to canvas device
// pixels (the buffer's space) so the transform math stays in one space.
static void handle_canvas_input(App *a, const vv_Input *in, float dpi) {
  float lx = in->mouse.x - SIDEBAR_W; // logical, canvas-local
  float ly = in->mouse.y;
  bool inside = lx >= 0 && lx < (float)a->buf_w / dpi && ly >= 0 &&
                ly < (float)a->buf_h / dpi;

  float cx = lx * dpi, cy = ly * dpi; // canvas device px

  if (inside && in->wheel != 0.0f) {
    float factor = powf(1.1f, in->wheel);
    float nz = a->zoom * factor;
    if (nz < 0.02f)
      nz = 0.02f;
    if (nz > 256.0f)
      nz = 256.0f;
    // keep the document point under the cursor fixed as zoom changes.
    a->pan_x = cx - (cx - a->pan_x) * (nz / a->zoom);
    a->pan_y = cy - (cy - a->pan_y) * (nz / a->zoom);
    a->zoom = nz;
    a->dirty = true;
  }

  if (in->mouse_down && inside && !a->dragging) {
    a->dragging = true;
    a->drag_last = in->mouse;
  }
  if (a->dragging && in->mouse_down) {
    float dx = (in->mouse.x - a->drag_last.x) * dpi;
    float dy = (in->mouse.y - a->drag_last.y) * dpi;
    if (dx != 0 || dy != 0) {
      a->pan_x += dx;
      a->pan_y += dy;
      a->drag_last = in->mouse;
      a->dirty = true;
    }
  }
  if (!in->mouse_down)
    a->dragging = false;
}

// Load a TTF file into memory for craz font system.
static cr_font *load_font_for_craz(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  if (len <= 0) { fclose(f); return NULL; }
  fseek(f, 0, SEEK_SET);
  void *data = malloc((size_t)len);
  if (!data) { fclose(f); return NULL; }
  if (fread(data, 1, (size_t)len, f) != (size_t)len) {
    free(data); fclose(f); return NULL;
  }
  fclose(f);
  cr_font *font = cr_font_load(data, (int)len, 0);
  free(data);
  return font;
}

int main(int argc, char **argv) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 craz SVG viewer", 1180, 1400);
  if (!app)
    return 1;
  for (const char *const *f = vv_default_font_paths(); *f; f++)
    if (vv_app_load_font(app, *f))
      break;

  vv_Ctx ctx;
  vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);

  long hw = sysconf(_SC_NPROCESSORS_ONLN);
  App a = {.app = app,
           .zoom = 1.0f,
           .selected_sample = -1,
           .max_threads = hw > 0 ? (hw > 64 ? 64 : (int)hw) : 1,
           .continuous = true,
           .font_size = 32.0f,
           .debug_open = true};
  a.nthreads = a.max_threads > 4 ? 4 : a.max_threads;
  reconfigure_threads(&a, a.nthreads);

  // Load a font for the debug panel
  const char *font_paths[] = {
    "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
    "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    NULL
  };
  a.bake_ctx = cr_context_new();
  for (int i = 0; font_paths[i]; i++) {
    a.font = load_font_for_craz(font_paths[i]);
    if (a.font) {
      printf("Loaded font: %s\n", font_paths[i]);
      break;
    }
  }
  if (a.font) {
    a.glyph_cache = cr_glyph_cache_new(a.font, 512, 512, 4);
    // Pre-render some default text to populate the atlas
    snprintf(a.debug_text, sizeof a.debug_text, "Hello, craz!");
    render_debug_text(&a);
    update_atlas_texture(&a);
  }

  if (argc > 1)
    load_svg(&a, argv[1]);
  else
    load_svg(&a, SAMPLES[0].path), a.selected_sample = 0;

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();

  while (vv_app_pump(app, &in)) {
    uint64_t nowc = SDL_GetPerformanceCounter();
    float dt = (float)(nowc - prev) / (float)SDL_GetPerformanceFrequency();
    prev = nowc;
    if (dt > 0.1f)
      dt = 0.1f;

    int w, h;
    float dpi;
    vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    handle_canvas_input(&a, &in, dpi);

    // canvas backing size in device pixels
    int cw = (int)lroundf(((float)w - SIDEBAR_W) * dpi);
    int ch = (int)lroundf((float)h * dpi);

    if (a.dirty || a.continuous) {
      update_canvas(&a, cw, ch);
      a.dirty = false;
    }

    // build the UI, then drain widget events into update() for the next frame.
    vv_begin_frame(&ctx, dt, &in);
    view(&ctx, &a);
    vv_CommandBuffer *cmds = vv_end_frame(&ctx);

    vv_Event ev;
    while (vv_poll_event(&ctx, &ev))
      update(&a, ev);

    vv_app_frame_begin(app, vv_rgb(0.10f, 0.11f, 0.13f));
    vv_render(vv_app_backend(app), cmds, w, h, dpi);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    vv_app_frame_end(app);
  }

  if (a.have_tex)
    vv_app_texture_destroy(app, a.tex);
  if (a.atlas_tex_valid)
    vv_app_texture_destroy(app, a.atlas_tex);
  pool_destroy(a.pool);
  for (int i = 0; i < a.cfg_threads; i++)
    cr_context_free(a.ctxs[i]);
  free(a.ctxs);
  free(a.buf);
  free(a.atlas_rgba);
  if (a.doc)
    cr_svg_free(a.doc);
  if (a.glyph_cache)
    cr_glyph_cache_free(a.glyph_cache);
  if (a.font)
    cr_font_free(a.font);
  if (a.bake_ctx)
    cr_context_free(a.bake_ctx);
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
