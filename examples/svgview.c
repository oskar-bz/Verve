1	// svgview.c — a real-time SVG viewer built on the Verve UI + the craz CPU
2	// vector rasterizer (../craz). It parses an SVG, rasterizes it on the CPU each
3	// frame with a configurable number of worker threads, uploads the result as a
4	// texture, and shows it in a pan/zoomable canvas alongside live timing metrics.
5	//
6	// Extended with a font/atlas debug panel: load a TTF, render text through the
7	// craz glyph cache, and visualize the skyline rectangle packing + glyph atlas
8	// in real-time — useful for debugging the atlas packer.
9	//
10	//   Build:  make svgview   ->   ./build/svgview [file.svg]
11	//
12	// Interaction (over the canvas):
13	//   • scroll wheel  — zoom toward the cursor
14	//   • left drag     — pan
15	//   • the sidebar   — pick a sample, set thread count, toggle continuous render
16	//
17	// Why a manual loop (not vv_app_run): we need the backend handle every frame to
18	// (re)upload the rasterized texture, and we want to control the exact order of
19	// "handle input -> rasterize -> build view -> present". So this drives the
20	// begin/build/end + present loop itself and drains widget events by hand.
21	#define _POSIX_C_SOURCE 200112L
22	#include "vv_sdl_gl.h"
23	#include "craz/svg.h"
24	#include "craz/font.h"
25	#include "craz/bake.h"
26	
27	#include <SDL3/SDL.h>
28	#include <pthread.h>
29	#include <stdatomic.h>
30	#include <stdbool.h>
31	#include <stdio.h>
32	#include <stdlib.h>
33	#include <string.h>
34	#include <math.h>
35	#include <unistd.h>
36	#include <ctype.h>
37	
38	// ---------------------------------------------------------------- thread pool
39	// A persistent pool of exactly N workers implementing craz's cr_dispatch_fn.
40	// Persistent (not spawn-per-frame) so the thread-count metric reflects raster
41	// cost, not pthread_create overhead. Workers claim band indices atomically; the
42	// calling (main) thread blocks until the job's items all complete.
43	typedef struct {
44	  pthread_t *threads;
45	  int n;
46	  pthread_mutex_t mtx;
47	  pthread_cond_t work_cv, done_cv;
48	  void (*fn)(void *, int);
49	  void *fn_ctx;
50	  int count;
51	  atomic_int next;
52	  int active;    // workers still running the current job
53	  uint64_t gen;  // job generation; bumped to release the workers
54	  bool stop;
55	} Pool;
56	
57	static void *pool_worker(void *arg) {
58	  Pool *p = arg;
59	  pthread_mutex_lock(&p->mtx);
60	  uint64_t seen = p->gen;
61	  for (;;) {
62	    while (!p->stop && p->gen == seen)
63	      pthread_cond_wait(&p->work_cv, &p->mtx);
64	    if (p->stop) {
65	      pthread_mutex_unlock(&p->mtx);
66	      return NULL;
67	    }
68	    seen = p->gen;
69	    void (*fn)(void *, int) = p->fn;
70	    void *fc = p->fn_ctx;
71	    int count = p->count;
72	    pthread_mutex_unlock(&p->mtx);
73	
74	    int i;
75	    while ((i = atomic_fetch_add(&p->next, 1)) < count)
76	      fn(fc, i);
77	
78	    pthread_mutex_lock(&p->mtx);
79	    if (--p->active == 0)
80	      pthread_cond_signal(&p->done_cv);
81	  }
82	}
83	
84	// cr_dispatch_fn: run fn(fn_ctx, i) for every i in [0,count) across the workers.
85	static void pool_dispatch(void *user, void (*fn)(void *, int), void *fn_ctx,
86	                          int count) {
87	  Pool *p = user;
88	  pthread_mutex_lock(&p->mtx);
89	  p->fn = fn;
90	  p->fn_ctx = fn_ctx;
91	  p->count = count;
92	  atomic_store(&p->next, 0);
93	  p->active = p->n;
94	  p->gen++;
95	  pthread_cond_broadcast(&p->work_cv);
96	  while (p->active != 0)
97	    pthread_cond_wait(&p->done_cv, &p->mtx);
98	  pthread_mutex_unlock(&p->mtx);
99	}
100	
101	static Pool *pool_create(int n) {
102	  Pool *p = calloc(1, sizeof *p);
103	  p->n = n;
104	  pthread_mutex_init(&p->mtx, NULL);
105	  pthread_cond_init(&p->work_cv, NULL);
106	  pthread_cond_init(&p->done_cv, NULL);
107	  atomic_init(&p->next, 0);
108	  p->threads = calloc((size_t)n, sizeof(pthread_t));
109	  for (int i = 0; i < n; i++)
110	    pthread_create(&p->threads[i], NULL, pool_worker, p);
111	  return p;
112	}
113	
114	static void pool_destroy(Pool *p) {
115	  if (!p)
116	    return;
117	  pthread_mutex_lock(&p->mtx);
118	  p->stop = true;
119	  p->gen++;
120	  pthread_cond_broadcast(&p->work_cv);
121	  pthread_mutex_unlock(&p->mtx);
122	  for (int i = 0; i < p->n; i++)
123	    pthread_join(p->threads[i], NULL);
124	  pthread_mutex_destroy(&p->mtx);
125	  pthread_cond_destroy(&p->work_cv);
126	  pthread_cond_destroy(&p->done_cv);
127	  free(p->threads);
128	  free(p);
129	}
130	
131	// ---------------------------------------------------------------------- state
132	enum {
133	  MSG_OPEN = 1, // open the OS file picker
134	  MSG_SAMPLE,   // load sample svg (payload as_int = index into SAMPLES)
135	  MSG_THREADS,  // thread-count slider (payload as_float)
136	  MSG_CONTINUOUS,
137	  MSG_RESET, // reset pan/zoom to fit
138	  MSG_DEBUG_TEXT,   // debug text input changed
139	  MSG_DEBUG_TOGGLE, // toggle debug panel
140	  MSG_FONT_SIZE,   // font size slider
141	};
142	
143	typedef struct {
144	  const char *label;
145	  const char *path;
146	} Sample;
147	
148	// Sample documents shipped with craz. Paths are relative to ../craz.
149	static const Sample SAMPLES[] = {
150	    {"Tiger", "../craz/svgs/tiger.svg"},
151	    {"Boston map", "../craz/svgs/boston.svg"},
152	    {"Periodic table", "../craz/svgs/periodic-table.svg"},
153	    {"Paris (30k paths)", "../craz/svgs/paris-30k.svg"},
154	    {"Paragraphs", "../craz/svgs/paragraphs.svg"},
155	};
156	enum { NSAMPLES = (int)(sizeof SAMPLES / sizeof SAMPLES[0]) };
157	
158	#define METRIC_HISTORY 180
159	#define SIDEBAR_W 300.0f
160	#define ATLAS_ZOOM 3.0f        // zoom factor for the atlas preview
161	#define ATLAS_PREVIEW_H 240.0f  // max height of atlas preview in the sidebar
162	#define DEBUG_TEXT_CAP 256
163	
164	typedef struct {
165	  vv_App *app;
166	
167	  // document
168	  cr_svg *doc;
169	  char path[512];
170	  char err[256];
171	  float svg_w, svg_h;
172	
173	  // view transform, in canvas device pixels (pan) + scalar zoom
174	  float zoom;
175	  float pan_x, pan_y;
176	  int selected_sample; // -1 if none
177	
178	  // rasterization
179	  int nthreads, max_threads;
180	  bool continuous;
181	  cr_context **ctxs; // one per band; count == cfg_threads
182	  int cfg_threads;   // thread count the ctxs/pool are currently built for
183	  Pool *pool;        // NULL when cfg_threads <= 1 (serial)
184	
185	  // canvas backing store + texture
186	  uint8_t *buf;
187	  int buf_w, buf_h;
188	  vv_TexID tex;
189	  vv_ImageRef img;
190	  bool have_tex;
191	
192	  // metrics
193	  bool dirty;
194	  double last_ms;
195	  float ms_hist[METRIC_HISTORY];
196	  int ms_count;
197	
198	  // input tracking
199	  bool dragging;
200	  vv_Vec2 drag_last;
201	
202	  // -------------------------------------------------------- font debug state
203	  cr_font *font;
204	  cr_glyph_cache *glyph_cache;
205	  cr_context *bake_ctx;  // single context for baking glyphs
206	  float font_size;
207	  bool debug_open;       // debug panel visibility
208	  char debug_text[DEBUG_TEXT_CAP];
209	
210	  // atlas display texture (converted A8->RGBA)
211	  uint8_t *atlas_rgba;
212	  int atlas_rgba_w, atlas_rgba_h;
213	  vv_TexID atlas_tex;
214	  vv_ImageRef atlas_img;
215	  bool atlas_tex_valid;
216	
217	  // cached debug info from last frame
218	  int cache_entry_count;
219	  float atlas_fill_ratio;
220	} App;
221	
222	// ------------------------------------------------------------- raster helpers
223	static double now_ms(void) {
224	  return (double)SDL_GetPerformanceCounter() * 1000.0 /
225	         (double)SDL_GetPerformanceFrequency();
226	}
227	
228	// Rebuild the per-band contexts (and the pool) for `n` threads.
229	static void reconfigure_threads(App *a, int n) {
230	  if (n < 1)
231	    n = 1;
232	  if (a->ctxs) {
233	    for (int i = 0; i < a->cfg_threads; i++)
234	      cr_context_free(a->ctxs[i]);
235	    free(a->ctxs);
236	  }
237	  pool_destroy(a->pool);
238	  a->pool = NULL;
239	
240	  a->cfg_threads = n;
241	  a->ctxs = calloc((size_t)n, sizeof(cr_context *));
242	  for (int i = 0; i < n; i++)
243	    a->ctxs[i] = cr_context_new();
244	  if (n > 1)
245	    a->pool = pool_create(n);
246	}
247	
248	// Paint a checkerboard into the canvas buffer so a transparent SVG reads
249	// clearly. Opaque (a=255), so the uploaded texture needs no alpha compositing.
250	static void fill_checker(uint8_t *buf, int w, int h) {
251	  const int cell = 12;
252	  for (int y = 0; y < h; y++) {
253	    uint8_t *row = buf + (size_t)y * w * 4;
254	    int yc = (y / cell) & 1;
255	    for (int x = 0; x < w; x++) {
256	      uint8_t v = ((x / cell) & 1) ^ yc ? 46 : 38;
257	      row[x * 4 + 0] = v;
258	      row[x * 4 + 1] = v + 2;
259	      row[x * 4 + 2] = v + 5;
260	      row[x * 4 + 3] = 255;
261	    }
262	  }
263	}
264	
265	static void load_svg(App *a, const char *path) {
266	  cr_svg *doc = cr_svg_parse_file(path);
267	  if (!doc) {
268	    snprintf(a->err, sizeof a->err, "failed to parse %s", path);
269	    return;
270	  }
271	  if (a->doc)
272	    cr_svg_free(a->doc);
273	  a->doc = doc;
274	  a->err[0] = 0;
275	  snprintf(a->path, sizeof a->path, "%s", path);
276	  a->svg_w = cr_svg_width(doc);
277	  a->svg_h = cr_svg_height(doc);
278	  a->zoom = 1.0f; // reset to fit
279	  a->pan_x = a->pan_y = 0.0f;
280	  a->dirty = true;
281	}
282	
283	// Rasterize the document into `buf` (sized cw x ch, device pixels) with the
284	// current pan/zoom, timing the raster call. Returns wall-clock milliseconds.
285	static double rasterize(App *a, int cw, int ch) {
286	  cr_surface s = {a->buf, cw, ch, cw * 4, CR_FORMAT_RGBA8_PREMUL};
287	  fill_checker(a->buf, cw, ch);
288	  if (!a->doc)
289	    return 0.0;
290	
291	  // user units -> device: fit-to-canvas, then apply zoom and pan (device px).
292	  cr_matrix fit = cr_svg_fit(a->doc, (float)cw, (float)ch);
293	  cr_matrix m = cr_matrix_mul(fit, cr_matrix_scale(a->zoom, a->zoom));
294	  m = cr_matrix_mul(m, cr_matrix_translate(a->pan_x, a->pan_y));
295	
296	  double t0 = now_ms();
297	  if (a->cfg_threads <= 1 || !a->pool) {
298	    cr_svg_render(a->doc, a->ctxs[0], &s, &m);
299	  } else {
300	    cr_svg_render_tiled(a->doc, (cr_context *const *)a->ctxs, a->cfg_threads, &s,
301	                        &m, pool_dispatch, a->pool);
302	  }
303	  return now_ms() - t0;
304	}
305	
306	// Re-raster (resizing the buffer to the canvas) and re-upload the texture.
307	static void update_canvas(App *a, int cw, int ch) {
308	  if (cw < 1)
309	    cw = 1;
310	  if (ch < 1)
311	    ch = 1;
312	  if (cw != a->buf_w || ch != a->buf_h) {
313	    free(a->buf);
314	    a->buf = malloc((size_t)cw * ch * 4);
315	    a->buf_w = cw;
316	    a->buf_h = ch;
317	    a->dirty = true;
318	  }
319	  if (a->cfg_threads != a->nthreads)
320	    reconfigure_threads(a, a->nthreads);
321	
322	  double ms = rasterize(a, cw, ch);
323	  a->last_ms = ms;
324	  if (a->ms_count < METRIC_HISTORY) {
325	    a->ms_hist[a->ms_count++] = (float)ms;
326	  } else {
327	    memmove(a->ms_hist, a->ms_hist + 1, sizeof(float) * (METRIC_HISTORY - 1));
328	    a->ms_hist[METRIC_HISTORY - 1] = (float)ms;
329	  }
330	
331	  if (a->have_tex)
332	    vv_app_texture_destroy(a->app, a->tex);
333	  a->tex = vv_app_texture_from_rgba(a->app, a->buf, cw, ch);
334	  a->have_tex = true;
335	  a->img = (vv_ImageRef){.tex = a->tex,
336	                         .uv = {0, 0, 1, 1},
337	                         .tint = {1, 1, 1, 1}};
338	}
339	
340	// ---------------------------------------------------------- font debug helpers
341	
342	// Convert A8 coverage to RGBA (white text on a dark background).
343	static void atlas_a8_to_rgba(const uint8_t *a8, uint8_t *rgba,
344	                              int w, int h, int a8_stride, int rgba_stride) {
345	  for (int y = 0; y < h; y++) {
346	    for (int x = 0; x < w; x++) {
347	      uint8_t v = a8[y * a8_stride + x];
348	      rgba[y * rgba_stride + x * 4 + 0] = v;
349	      rgba[y * rgba_stride + x * 4 + 1] = v;
350	      rgba[y * rgba_stride + x * 4 + 2] = v;
351	      rgba[y * rgba_stride + x * 4 + 3] = 255;
352	    }
353	  }
354	}
355	
356	// Render all codepoints in the debug text buffer through the glyph cache.
357	// This populates the atlas with new glyphs on cache misses.
358	static void render_debug_text(App *a) {
359	  if (!a->glyph_cache || !a->bake_ctx || !a->debug_text[0])
360	    return;
361	  const char *p = a->debug_text;
362	  float pen_x = 0.0f;
363	  float line_height = a->font_size * 1.3f;
364	  while (*p) {
365	    int cp = (unsigned char)*p;
366	    int len = 1;
367	    if ((cp & 0xe0) == 0xc0) {
368	      cp = ((int)p[0] & 0x1f) << 6;
369	      len = 2;
370	      if (p[1]) cp |= p[1] & 0x3f;
371	    } else if ((cp & 0xf0) == 0xe0) {
372	      cp = ((int)p[0] & 0x0f) << 12;
373	      len = 3;
374	      if (p[1]) cp |= ((int)p[1] & 0x3f) << 6;
375	      if (p[2]) cp |= p[2] & 0x3f;
376	    } else if ((cp & 0xf8) == 0xf0) {
377	      cp = ((int)p[0] & 0x07) << 18;
378	      len = 4;
379	      if (p[1]) cp |= ((int)p[1] & 0x3f) << 12;
380	      if (p[2]) cp |= ((int)p[2] & 0x3f) << 6;
381	      if (p[3]) cp |= p[3] & 0x3f;
382	    }
383	    if (cp == '\n') {
384	      pen_x = 0.0f;
385	      p += len;
386	      continue;
387	    }
388	    cr_glyph_entry entry;
389	    if (cr_glyph_cache_get(a->glyph_cache, a->bake_ctx, cp, a->font_size, pen_x, &entry)) {
390	      pen_x += entry.advance;
391	    } else {
392	      pen_x += a->font_size * 0.5f;
393	    }
394	    p += len;
395	  }
396	}
397	
398	// Update the atlas display texture (convert A8 -> RGBA and upload to GPU).
399	static void update_atlas_texture(App *a) {
400	  if (!a->glyph_cache)
401	    return;
402	  int aw, ah, stride;
403	  const uint8_t *a8 = cr_glyph_cache_pixels(a->glyph_cache, &aw, &ah, &stride);
404	  if (!a8 || aw <= 0 || ah <= 0)
405	    return;
406	
407	  if (aw != a->atlas_rgba_w || ah != a->atlas_rgba_h) {
408	    free(a->atlas_rgba);
409	    a->atlas_rgba = malloc((size_t)aw * ah * 4);
410	    a->atlas_rgba_w = aw;
411	    a->atlas_rgba_h = ah;
412	  }
413	
414	  atlas_a8_to_rgba(a8, a->atlas_rgba, aw, ah, stride, aw * 4);
415	
416	  if (a->atlas_tex_valid)
417	    vv_app_texture_destroy(a->app, a->atlas_tex);
418	  a->atlas_tex = vv_app_texture_from_rgba(a->app, a->atlas_rgba, aw, ah);
419	  a->atlas_tex_valid = true;
420	  a->atlas_img = (vv_ImageRef){.tex = a->atlas_tex,
421	                               .uv = {0, 0, 1, 1},
422	                               .tint = {1, 1, 1, 1}};
423	
424	  // Compute stats
425	  cr_atlas_debug ad;
426	  const cr_atlas *atlas = cr_glyph_cache_get_atlas(a->glyph_cache);
427	  a->cache_entry_count = 0;
428	  a->atlas_fill_ratio = 0.0f;
429	  if (cr_atlas_debug_info(atlas, &ad) && ad.w > 0 && ad.h > 0) {
430	    int max_y = 0;
431	    for (int i = 0; i < ad.nnodes; i++)
432	      if (ad.nodes[i].y > max_y) max_y = ad.nodes[i].y;
433	    a->atlas_fill_ratio = (float)max_y / (float)ad.h;
434	    a->cache_entry_count = cr_glyph_cache_enumerate(a->glyph_cache, NULL, NULL);
435	  }
436	}
437	
438	// ------------------------------------------------------------------- update()
439	static void on_file_picked(void *ud, const char *path) {
440	  if (path)
441	    load_svg((App *)ud, path);
442	}
443	
444	static void update(void *state, vv_Event ev) {
445	  App *a = state;
446	  switch (ev.msg) {
447	  case MSG_OPEN:
448	    vv_app_open_file(a->app, "SVG", "svg", on_file_picked, a);
449	    break;
450	  case MSG_SAMPLE: {
451	    int i = (int)ev.data.as_int;
452	    if (i >= 0 && i < NSAMPLES) {
453	      a->selected_sample = i;
454	      load_svg(a, SAMPLES[i].path);
455	    }
456	    break;
457	  }
458	  case MSG_THREADS: {
459	    int n = (int)lroundf((float)ev.data.as_float);
460	    if (n < 1) n = 1;
461	    if (n > a->max_threads) n = a->max_threads;
462	    if (n != a->nthreads) { a->nthreads = n; a->dirty = true; }
463	    break;
464	  }
465	  case MSG_CONTINUOUS:
466	    a->continuous = ev.data.as_int != 0;
467	    break;
468	  case MSG_RESET:
469	    a->zoom = 1.0f; a->pan_x = a->pan_y = 0.0f; a->dirty = true;
470	    break;
471	  case MSG_DEBUG_TEXT:
472	    render_debug_text(a);
473	    update_atlas_texture(a);
474	    break;
475	  case MSG_DEBUG_TOGGLE:
476	    a->debug_open = ev.data.as_int != 0;
477	    break;
478	  case MSG_FONT_SIZE: {
479	    float sz = (float)ev.data.as_float;
480	    if (sz < 8.0f) sz = 8.0f;
481	    if (sz > 256.0f) sz = 256.0f;
482	    if (sz != a->font_size) {
483	      a->font_size = sz;
484	      if (a->glyph_cache) { cr_glyph_cache_free(a->glyph_cache); a->glyph_cache = NULL; }
485	      if (a->font)
486	        a->glyph_cache = cr_glyph_cache_new(a->font, 512, 512, 4);
487	      a->atlas_tex_valid = false;
488	      render_debug_text(a);
489	      update_atlas_texture(a);
490	    }
491	    break;
492	  }
493	  default: break;
494	  }
495	}
496	
497	// Callback for cr_glyph_cache_enumerate: draw a colored outline around each
498	// glyph's atlas rect, scaled to the preview coordinates.
499	struct GlyphVisit { vv_Ctx *ctx; uint32_t node; float sx, sy; };
500	static void draw_glyph_outline(void *ud, const cr_glyph_entry *e) {
501	  struct GlyphVisit *gv = (struct GlyphVisit *)ud;
502	  if (e->w <= 0 || e->h <= 0) return;
503	  float x = (float)e->x * gv->sx;
504	  float y = (float)e->y * gv->sy;
505	  float w = (float)e->w * gv->sx;
506	  float h = (float)e->h * gv->sy;
507	  vv_Vec2 pts[5] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, {x, y}};
508	  vv_draw_polyline(gv->ctx, gv->node, pts, 5, 1.0f,
509	                   (vv_Color){0.2f, 0.8f, 1.0f, 0.7f});
510	}
511	
512	// --------------------------------------------------------------------- view()
513	
514	static void metric_row(vv_Ctx *c, const vv_Theme *t, const char *k,
515	                       const char *v) {
516	  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
517	                      .main = VV_ALIGN_SPACE_BETWEEN, .cross = VV_ALIGN_CENTER),
518	         VV_STYLE(.bg = {0})) {
519	    vv_text(c, k, VV_STYLE(.fg = t->text_secondary, .font_size = 13));
520	    vv_text(c, v, VV_STYLE(.fg = t->text_primary, .font_size = 13));
521	  }
522	}
523	
524	static void view(vv_Ctx *c, void *state) {
525	  App *a = state;
526	  const vv_Theme *t = vv_theme();
527	
528	  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)),
529	         VV_STYLE(.bg = t->surface_app)) {
530	
531	    // ---- sidebar ----------------------------------------------------------
532	    VV_BOX(c,
533	           VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(SIDEBAR_W), .h = vv_grow(1),
534	                     .padding = vv_all(16), .gap = 12, .scroll_y = true),
535	           VV_STYLE(.bg = t->surface_panel,
536	                    .border_width = {0, 0, 1, 0},
537	                    .border_color = t->border_subtle)) {
538	      vv_text(c, "craz \xc2\xb7 SVG viewer",
539	              VV_STYLE(.fg = t->text_primary, .font_size = 20));
540	      vv_text(c, "CPU vector rasterizer",
541	              VV_STYLE(.fg = t->text_muted, .font_size = 12));
542	
543	      vv_button(c, "open", "Open SVG\xe2\x80\xa6", MSG_OPEN, VV_NO_PAYLOAD);
544	
545	      vv_text(c, "SAMPLES",
546	              VV_STYLE(.fg = t->text_muted, .font_size = 11));
547	      for (int i = 0; i < NSAMPLES; i++)
548	        vv_list_item(c, SAMPLES[i].path, SAMPLES[i].label,
549	                     a->selected_sample == i, MSG_SAMPLE, vv_pi(i));
550	
551	      VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
552	             VV_STYLE(.bg = t->border_subtle)) {}
553	
554	      // threads
555	      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
556	                          .main = VV_ALIGN_SPACE_BETWEEN),
557	             VV_STYLE(.bg = {0})) {
558	        vv_text(c, "Threads", VV_STYLE(.fg = t->text_secondary, .font_size = 13));
559	        vv_text(c, vv_fmt(c, "%d / %d", a->nthreads, a->max_threads),
560	                VV_STYLE(.fg = t->brand_primary, .font_size = 13));
561	      }
562	      vv_slider(c, "threads", (float)a->nthreads, 1.0f, (float)a->max_threads,
563	                MSG_THREADS);
564	
565	      vv_checkbox(c, "cont", "Continuous render", a->continuous, MSG_CONTINUOUS);
566	      vv_button(c, "reset", "Reset view", MSG_RESET, VV_NO_PAYLOAD);
567	
568	      VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
569	             VV_STYLE(.bg = t->border_subtle)) {}
570	
571	      // metrics
572	      vv_text(c, "METRICS", VV_STYLE(.fg = t->text_muted, .font_size = 11));
573	
574	      double px = (double)a->buf_w * (double)a->buf_h;
575	      double fps = a->last_ms > 0 ? 1000.0 / a->last_ms : 0.0;
576	      double mpx = a->last_ms > 0 ? px / (a->last_ms / 1000.0) / 1e6 : 0.0;
577	      double avg = 0;
578	      for (int i = 0; i < a->ms_count; i++)
579	        avg += a->ms_hist[i];
580	      if (a->ms_count)
581	        avg /= a->ms_count;
582	
583	      metric_row(c, t, "Canvas", vv_fmt(c, "%d\xc3\x97%d", a->buf_w, a->buf_h));
584	      if (a->doc)
585	        metric_row(c, t, "Document",
586	                   vv_fmt(c, "%.0f\xc3\x97%.0f", a->svg_w, a->svg_h));
587	      metric_row(c, t, "Zoom", vv_fmt(c, "%.0f%%", a->zoom * 100.0f));
588	      metric_row(c, t, "Raster", vv_fmt(c, "%.2f ms", a->last_ms));
589	      metric_row(c, t, "Average", vv_fmt(c, "%.2f ms", avg));
590	      metric_row(c, t, "Rate", vv_fmt(c, "%.0f fps", fps));
591	      metric_row(c, t, "Throughput", vv_fmt(c, "%.0f Mpx/s", mpx));
592	
593	      if (a->ms_count > 1) {
594	        vv_PlotSeries series = {.ys = a->ms_hist,
595	                                .count = a->ms_count,
596	                                .color = t->brand_primary,
597	                                .kind = VV_PLOT_LINE,
598	                                .name = "raster ms"};
599	        vv_plot(c, "hist", &series, 1,
600	                (vv_PlotOpts){.auto_y = true, .grid = true, .height = 90});
601	      }
602	
603	      if (a->err[0])
604	        vv_text(c, a->err, VV_STYLE(.fg = t->status_error, .font_size = 12));
605	
606	      // --------------------------------------------------------- debug panel
607	      VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
608	             VV_STYLE(.bg = t->border_subtle)) {}
609	
610	      vv_checkbox(c, "dbg", "Atlas debug", a->debug_open, MSG_DEBUG_TOGGLE);
611	
612	      if (a->debug_open && a->font) {
613	        vv_text(c, "DEBUG TEXT",
614	                VV_STYLE(.fg = t->text_muted, .font_size = 11));
615	        vv_text_field(c, "dbgtext", a->debug_text, DEBUG_TEXT_CAP,
616	                      "Type text here...", MSG_DEBUG_TEXT);
617	
618	        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
619	                            .main = VV_ALIGN_SPACE_BETWEEN),
620	               VV_STYLE(.bg = {0})) {
621	          vv_text(c, "Size", VV_STYLE(.fg = t->text_secondary, .font_size = 13));
622	          vv_text(c, vv_fmt(c, "%.0f", a->font_size),
623	                  VV_STYLE(.fg = t->brand_primary, .font_size = 13));
624	        }
625	        vv_slider(c, "fontsize", a->font_size, 8.0f, 256.0f, MSG_FONT_SIZE);
626	
627	        VV_BOX(c, VV_LAYOUT(.h = vv_fixed(1), .w = vv_grow(1)),
628	               VV_STYLE(.bg = t->border_subtle)) {}
629	        vv_text(c, "ATLAS STATS",
630	                VV_STYLE(.fg = t->text_muted, .font_size = 11));
631	
632	        if (a->atlas_tex_valid) {
633	          metric_row(c, t, "Size",
634	                     vv_fmt(c, "%d\xc3\x97%d", a->atlas_rgba_w, a->atlas_rgba_h));
635	          metric_row(c, t, "Entries", vv_fmt(c, "%d", a->cache_entry_count));
636	          metric_row(c, t, "Fill",
637	                     vv_fmt(c, "%.0f%%", a->atlas_fill_ratio * 100.0f));
638	
639	          // Compute atlas preview size (zoom, capped height)
640	          float preview_w = (float)a->atlas_rgba_w * ATLAS_ZOOM;
641	          float preview_h = (float)a->atlas_rgba_h * ATLAS_ZOOM;
642	          if (preview_h > ATLAS_PREVIEW_H) {
643	            float scale = ATLAS_PREVIEW_H / preview_h;
644	            preview_w *= scale;
645	            preview_h *= scale;
646	          }
647	
648	          // Draw atlas texture
649	          uint32_t img_node = vv_image(c, "atlas", &a->atlas_img,
650	                                       vv_fixed(preview_w), vv_fixed(preview_h));
651	
652	          // Draw skyline nodes and glyph rects as overlays on the image
653	          const cr_atlas *atlas = cr_glyph_cache_get_atlas(a->glyph_cache);
654	          cr_atlas_debug ad;
655	          if (cr_atlas_debug_info(atlas, &ad) && ad.nnodes > 0) {
656	            float sx = preview_w / (float)ad.w;
657	            float sy = preview_h / (float)ad.h;
658	
659	            // Skyline nodes: each is a segment at y=(node.y) spanning node.width
660	            for (int i = 0; i < ad.nnodes; i++) {
661	              float nx = (float)ad.nodes[i].x * sx;
662	              float ny = (float)ad.nodes[i].y * sy;
663	              float nw = (float)ad.nodes[i].width * sx;
664	              vv_Vec2 pts[4] = {
665	                {nx, ny}, {nx + nw, ny},
666	                {nx + nw, ny + 2.0f}, {nx, ny + 2.0f}
667	              };
668	              vv_draw_polygon(c, img_node, pts, 4, (vv_Color){1, 0, 0.5f, 0.6f});
669	            }
670	
671	            // Enumerate cached glyphs and draw their rects
672	            struct GlyphVisit { vv_Ctx *ctx; uint32_t node; float sx, sy; };
673	            struct GlyphVisit gv = { .ctx = c, .node = img_node, .sx = sx, .sy = sy };
674	            cr_glyph_cache_enumerate(a->glyph_cache, draw_glyph_outline, &gv);
675	          }
676	        } else {
677	          vv_text(c, "(type some text to see the atlas)",
678	                  VV_STYLE(.fg = t->text_muted, .font_size = 12));
679	        }
680	      } else if (a->debug_open && !a->font) {
681	        vv_text(c, "(no font loaded)",
682	                VV_STYLE(.fg = t->status_error, .font_size = 12));
683	      }
684	    }
685	
686	    // ---- canvas -----------------------------------------------------------
687	    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
688	                        .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
689	           VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.13f))) {
690	      if (a->have_tex) {
691	        vv_image(c, "canvas", &a->img, vv_grow(1), vv_grow(1));
692	      } else {
693	        vv_text(c, "Open an SVG or pick a sample \xe2\x86\x90",
694	                VV_STYLE(.fg = t->text_muted, .font_size = 16));
695	      }
696	    }
697	  }
698	}
699	
700	// ----------------------------------------------------------------- main loop
701	// Handle wheel-zoom (toward cursor) and drag-pan while the cursor is over the
702	// canvas. Coordinates are converted from logical UI space to canvas device
703	// pixels (the buffer's space) so the transform math stays in one space.
704	static void handle_canvas_input(App *a, const vv_Input *in, float dpi) {
705	  float lx = in->mouse.x - SIDEBAR_W; // logical, canvas-local
706	  float ly = in->mouse.y;
707	  bool inside = lx >= 0 && lx < (float)a->buf_w / dpi && ly >= 0 &&
708	                ly < (float)a->buf_h / dpi;
709	
710	  float cx = lx * dpi, cy = ly * dpi; // canvas device px
711	
712	  if (inside && in->wheel != 0.0f) {
713	    float factor = powf(1.1f, in->wheel);
714	    float nz = a->zoom * factor;
715	    if (nz < 0.02f)
716	      nz = 0.02f;
717	    if (nz > 256.0f)
718	      nz = 256.0f;
719	    // keep the document point under the cursor fixed as zoom changes.
720	    a->pan_x = cx - (cx - a->pan_x) * (nz / a->zoom);
721	    a->pan_y = cy - (cy - a->pan_y) * (nz / a->zoom);
722	    a->zoom = nz;
723	    a->dirty = true;
724	  }
725	
726	  if (in->mouse_down && inside && !a->dragging) {
727	    a->dragging = true;
728	    a->drag_last = in->mouse;
729	  }
730	  if (a->dragging && in->mouse_down) {
731	    float dx = (in->mouse.x - a->drag_last.x) * dpi;
732	    float dy = (in->mouse.y - a->drag_last.y) * dpi;
733	    if (dx != 0 || dy != 0) {
734	      a->pan_x += dx;
735	      a->pan_y += dy;
736	      a->drag_last = in->mouse;
737	      a->dirty = true;
738	    }
739	  }
740	  if (!in->mouse_down)
741	    a->dragging = false;
742	}
743	
744	// Load a TTF file into memory for craz font system.
745	static cr_font *load_font_for_craz(const char *path) {
746	  FILE *f = fopen(path, "rb");
747	  if (!f) return NULL;
748	  fseek(f, 0, SEEK_END);
749	  long len = ftell(f);
750	  if (len <= 0) { fclose(f); return NULL; }
751	  fseek(f, 0, SEEK_SET);
752	  void *data = malloc((size_t)len);
753	  if (!data) { fclose(f); return NULL; }
754	  if (fread(data, 1, (size_t)len, f) != (size_t)len) {
755	    free(data); fclose(f); return NULL;
756	  }
757	  fclose(f);
758	  cr_font *font = cr_font_load(data, (int)len, 0);
759	  free(data);
760	  return font;
761	}
762	
763	int main(int argc, char **argv) {
764	  vv_App *app = vv_app_create("Verve \xc2\xb7 craz SVG viewer", 1180, 1400);
765	  if (!app)
766	    return 1;
767	  for (const char *const *f = vv_default_font_paths(); *f; f++)
768	    if (vv_app_load_font(app, *f))
769	      break;
770	
771	  vv_Ctx ctx;
772	  vv_init(&ctx);
773	  vv_set_measure_fn(&ctx, vv_app_measure, app);
774	
775	  long hw = sysconf(_SC_NPROCESSORS_ONLN);
776	  App a = {.app = app,
777	           .zoom = 1.0f,
778	           .selected_sample = -1,
779	           .max_threads = hw > 0 ? (hw > 64 ? 64 : (int)hw) : 1,
780	           .continuous = true,
781	           .font_size = 32.0f,
782	           .debug_open = true};
783	  a.nthreads = a.max_threads > 4 ? 4 : a.max_threads;
784	  reconfigure_threads(&a, a.nthreads);
785	
786	  // Load a font for the debug panel
787	  const char *font_paths[] = {
788	    "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
789	    "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
790	    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
791	    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
792	    "/usr/share/fonts/TTF/DejaVuSans.ttf",
793	    NULL
794	  };
795	  a.bake_ctx = cr_context_new();
796	  for (int i = 0; font_paths[i]; i++) {
797	    a.font = load_font_for_craz(font_paths[i]);
798	    if (a.font) {
799	      printf("Loaded font: %s\n", font_paths[i]);
800	      break;
801	    }
802	  }
803	  if (a.font) {
804	    a.glyph_cache = cr_glyph_cache_new(a.font, 512, 512, 4);
805	    // Pre-render some default text to populate the atlas
806	    snprintf(a.debug_text, sizeof a.debug_text, "Hello, craz!");
807	    render_debug_text(&a);
808	    update_atlas_texture(&a);
809	  }
810	
811	  if (argc > 1)
812	    load_svg(&a, argv[1]);
813	  else
814	    load_svg(&a, SAMPLES[0].path), a.selected_sample = 0;
815	
816	  vv_Input in = {0};
817	  uint64_t prev = SDL_GetPerformanceCounter();
818	
819	  while (vv_app_pump(app, &in)) {
820	    uint64_t nowc = SDL_GetPerformanceCounter();
821	    float dt = (float)(nowc - prev) / (float)SDL_GetPerformanceFrequency();
822	    prev = nowc;
823	    if (dt > 0.1f)
824	      dt = 0.1f;
825	
826	    int w, h;
827	    float dpi;
828	    vv_app_size(app, &w, &h, &dpi);
829	    vv_set_window(&ctx, (float)w, (float)h, dpi);
830	
831	    handle_canvas_input(&a, &in, dpi);
832	
833	    // canvas backing size in device pixels
834	    int cw = (int)lroundf(((float)w - SIDEBAR_W) * dpi);
835	    int ch = (int)lroundf((float)h * dpi);
836	
837	    if (a.dirty || a.continuous) {
838	      update_canvas(&a, cw, ch);
839	      a.dirty = false;
840	    }
841	
842	    // build the UI, then drain widget events into update() for the next frame.
843	    vv_begin_frame(&ctx, dt, &in);
844	    view(&ctx, &a);
845	    vv_CommandBuffer *cmds = vv_end_frame(&ctx);
846	
847	    vv_Event ev;
848	    while (vv_poll_event(&ctx, &ev))
849	      update(&a, ev);
850	
851	    vv_app_frame_begin(app, vv_rgb(0.10f, 0.11f, 0.13f));
852	    vv_render(vv_app_backend(app), cmds, w, h, dpi);
853	    vv_app_set_cursor(app, vv_cursor(&ctx));
854	    vv_app_frame_end(app);
855	  }
856	
857	  if (a.have_tex)
858	    vv_app_texture_destroy(app, a.tex);
859	  if (a.atlas_tex_valid)
860	    vv_app_texture_destroy(app, a.atlas_tex);
861	  pool_destroy(a.pool);
862	  for (int i = 0; i < a.cfg_threads; i++)
863	    cr_context_free(a.ctxs[i]);
864	  free(a.ctxs);
865	  free(a.buf);
866	  free(a.atlas_rgba);
867	  if (a.doc)
868	    cr_svg_free(a.doc);
869	  if (a.glyph_cache)
870	    cr_glyph_cache_free(a.glyph_cache);
871	  if (a.font)
872	    cr_font_free(a.font);
873	  if (a.bake_ctx)
874	    cr_context_free(a.bake_ctx);
875	  vv_shutdown(&ctx);
876	  vv_app_destroy(app);
877	  return 0;
878	}
879	