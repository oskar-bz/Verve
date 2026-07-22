// player.c — a "Now Playing" music player showcasing the craz vector stack that
// was just reintegrated: transport controls are crisp SVG icons (one A8 coverage
// mask per size, tinted on the GPU), and the album art is a live craz canvas — a
// vinyl record that actually spins while the track plays. The surrounding chrome
// (seek bar, volume, playlist) is ordinary Verve UI on the rich theme.
//
//   Build:  make player    ->    ./build/player
#include "verve/verve.h"
#include "vv_sdl_gl.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- a few inline SVG glyphs (24x24) --------------------------------------
static const char *SVG_PLAY  = "<svg viewBox='0 0 24 24'><path d='M8 5v14l11-7z'/></svg>";
static const char *SVG_PAUSE = "<svg viewBox='0 0 24 24'><path d='M6 5h4v14H6zM14 5h4v14h-4z'/></svg>";
static const char *SVG_PREV  = "<svg viewBox='0 0 24 24'><path d='M6 5h2v14H6zM20 5v14L9 12z'/></svg>";
static const char *SVG_NEXT  = "<svg viewBox='0 0 24 24'><path d='M16 5h2v14h-2zM4 5l11 7L4 19z'/></svg>";
static const char *SVG_HEART = "<svg viewBox='0 0 24 24'><path d='M12 21s-7.5-4.6-10-9.3C0.3 8.6 1.6 5 5 5c2 0 3.2 1.1 4 2.3C9.8 6.1 11 5 13 5c3.4 0 4.7 3.6 3 6.7C19.5 16.4 12 21 12 21z'/></svg>";
static const char *SVG_SHUF  = "<svg viewBox='0 0 24 24'><path d='M17 3l4 4-4 4v-3h-3l-2 3-2-3H3V6h6l2 3 2-3h3zM3 15h5l2-2 2 2h4v-3l4 4-4 4v-3h-5l-2-2-2 2H3z'/></svg>";

typedef struct { const char *title, *artist; float dur; vv_Color a, b; } Track;

static Track TRACKS[] = {
  { "Nightshade",     "Aurora Fields", 214, {0.55f,0.32f,0.85f,1}, {0.20f,0.42f,0.92f,1} },
  { "Copper Skies",   "Halcyon",       187, {0.95f,0.55f,0.25f,1}, {0.85f,0.22f,0.42f,1} },
  { "Tidal",          "Moss & Ivory",  253, {0.20f,0.72f,0.72f,1}, {0.28f,0.42f,0.85f,1} },
  { "Paper Lanterns", "Yuki",          166, {0.95f,0.72f,0.30f,1}, {0.92f,0.38f,0.55f,1} },
  { "Vellum",         "Slow Channel",  231, {0.45f,0.55f,0.95f,1}, {0.55f,0.30f,0.80f,1} },
};
#define NTRACK ((int)(sizeof TRACKS / sizeof TRACKS[0]))

enum { M_SEEK = 1, M_VOL, M_SELECT };

typedef struct {
  vv_App    *app;
  vv_Vector *vec;
  float      dpi;
  vv_Icon    play, pause, prev, next, heart, shuf;
  vv_Canvas *art;

  int    track;
  float  pos;       // playback position (s)
  float  vol;
  bool   playing, liked, shuffle;
  float  spin;      // vinyl rotation (accumulates only while playing)
} App;

static const char *mmss(char *buf, float s) {
  int v = (int)s; snprintf(buf, 8, "%d:%02d", v / 60, v % 60); return buf;
}

// ---- album art: a spinning vinyl rendered with craz ------------------------
static void redraw_art(App *a, int sz) {
  if (!a->art) a->art = vv_vector_canvas(a->vec, sz, sz);
  vv_canvas_resize(a->art, sz, sz);
  cr_context *ctx; cr_surface *s;
  vv_canvas_begin(a->art, &ctx, &s);

  float cx = sz * 0.5f, cy = sz * 0.5f, R = sz * 0.47f;
  Track *tr = &TRACKS[a->track];

  // disc body — near-black with a soft radial sheen
  cr_path *disc = cr_path_new(); cr_path_ellipse(disc, cx, cy, R, R);
  cr_gradient_stop body[2] = {
    { 0.0f, cr_rgba(38, 40, 48, 255) }, { 1.0f, cr_rgba(10, 11, 15, 255) } };
  cr_paint bp = cr_paint_radial(cx, cy - R * 0.2f, R, body, 2);
  cr_fill_path(ctx, s, NULL, disc, NULL, &bp, CR_FILL_NONZERO);

  // grooves
  cr_stroke_style gs = { .width = sz * 0.004f, .cap = CR_CAP_ROUND };
  cr_paint gp = cr_paint_color(cr_rgba(255, 255, 255, 14));
  for (int k = 0; k < 7; k++) {
    float gr = R * (0.42f + 0.075f * (float)k);
    cr_path *g = cr_path_new(); cr_path_ellipse(g, cx, cy, gr, gr);
    cr_stroke_path(ctx, s, NULL, g, NULL, &gp, &gs); cr_path_free(g);
  }

  // centre label — the track's gradient
  float lr = R * 0.38f;
  cr_path *label = cr_path_new(); cr_path_ellipse(label, cx, cy, lr, lr);
  cr_gradient_stop ls[2] = {
    { 0.0f, cr_rgba((uint8_t)(tr->a.r*255), (uint8_t)(tr->a.g*255), (uint8_t)(tr->a.b*255), 255) },
    { 1.0f, cr_rgba((uint8_t)(tr->b.r*255), (uint8_t)(tr->b.g*255), (uint8_t)(tr->b.b*255), 255) } };
  cr_paint lp = cr_paint_linear(cx - lr, cy - lr, cx + lr, cy + lr, ls, 2);
  cr_fill_path(ctx, s, NULL, label, NULL, &lp, CR_FILL_NONZERO);

  // a spin marker on the label so rotation reads, + centre hole
  float mx = cx + cosf(a->spin) * lr * 0.7f, my = cy + sinf(a->spin) * lr * 0.7f;
  cr_path *mk = cr_path_new(); cr_path_ellipse(mk, mx, my, sz * 0.018f, sz * 0.018f);
  cr_paint mp = cr_paint_color(cr_rgba(255, 255, 255, 220));
  cr_fill_path(ctx, s, NULL, mk, NULL, &mp, CR_FILL_NONZERO);
  cr_path *hole = cr_path_new(); cr_path_ellipse(hole, cx, cy, sz * 0.025f, sz * 0.025f);
  cr_paint hp = cr_paint_color(cr_rgba(12, 12, 16, 255));
  cr_fill_path(ctx, s, NULL, hole, NULL, &hp, CR_FILL_NONZERO);

  cr_path_free(disc); cr_path_free(label); cr_path_free(mk); cr_path_free(hole);
  vv_canvas_commit(a->art);
}

// A ghost icon button bound to this app's vector services (the built-in
// vv_icon_button does the box + hover/press + hit-test).
static bool icon_btn(vv_Ctx *c, App *a, const char *key, vv_Icon ic, float px,
                     vv_Color tint) {
  return vv_icon_button(c, a->vec, key, ic, px, a->dpi, tint, VV_ICON_BTN_GHOST);
}

static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  Track *tr = &TRACKS[a->track];
  char b1[8], b2[8];

  VV_BOX(c, VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1), .main = VV_ALIGN_CENTER,
                      .cross = VV_ALIGN_CENTER, .padding = vv_all(28)),
         VV_STYLE(.bg = vv_rgb(0.05f, 0.06f, 0.09f))) {
    // the player card
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(420), .gap = 20,
                        .padding = vv_all(28), .cross = VV_ALIGN_CENTER),
           VV_STYLE(.bg = t->surface_card, .radius = vv_r(22),
                    .border_width = vv_all(1), .border_color = t->border_subtle)) {
      // album art (craz canvas)
      const vv_ImageRef *r = vv_canvas_ref(a->art);
      if (r) vv_image(c, "art", r, vv_fixed(300), vv_fixed(300));

      // title + artist + like
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                          .gap = 12), VV_STYLE(.bg = {0})) {
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 3), VV_STYLE(.bg = {0})) {
          vv_text(c, tr->title, VV_STYLE(.fg = t->text, .font_size = 24));
          vv_text(c, tr->artist, VV_STYLE(.fg = t->text_muted, .font_size = 15));
        }
        if (icon_btn(c, a, "like", a->heart, 24,
                     a->liked ? vv_rgb(0.95f, 0.35f, 0.45f) : t->text_muted))
          a->liked = !a->liked;
      }

      // seek bar + times
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 6), VV_STYLE(.bg = {0})) {
        vv_slider(c, "seek", a->pos, 0.0f, tr->dur, M_SEEK);
        VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1),
                            .main = VV_ALIGN_SPACE_BETWEEN), VV_STYLE(.bg = {0})) {
          vv_text(c, mmss(b1, a->pos), VV_STYLE(.fg = t->text_muted, .font_size = 12));
          vv_text(c, mmss(b2, tr->dur), VV_STYLE(.fg = t->text_muted, .font_size = 12));
        }
      }

      // transport
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                          .main = VV_ALIGN_CENTER, .gap = 10), VV_STYLE(.bg = {0})) {
        if (icon_btn(c, a, "shuffle", a->shuf, 20,
                     a->shuffle ? t->accent : t->text_muted)) a->shuffle = !a->shuffle;
        if (icon_btn(c, a, "prev", a->prev, 24, t->text))
          { a->track = (a->track + NTRACK - 1) % NTRACK; a->pos = 0; }
        if (vv_icon_button(c, a->vec, "pp", a->playing ? a->pause : a->play, 30,
                           a->dpi, t->on_accent, VV_ICON_BTN_SOLID))
          a->playing = !a->playing;
        if (icon_btn(c, a, "next", a->next, 24, t->text))
          { a->track = (a->track + 1) % NTRACK; a->pos = 0; }
        // volume
        VV_BOX(c, VV_LAYOUT(.w = vv_fixed(84)), VV_STYLE(.bg = {0}))
          vv_slider(c, "vol", a->vol, 0.0f, 1.0f, M_VOL);
      }

      // playlist
      VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 2,
                          .padding = (vv_Edges){0, 8, 0, 0}), VV_STYLE(.bg = {0})) {
        for (int i = 0; i < NTRACK; i++) {
          bool cur = i == a->track;
          char k[8]; snprintf(k, sizeof k, "pl%d", i);
          uint32_t row = vv_box_keyed(c, k, strlen(k),
              VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                        .gap = 10, .padding = (vv_Edges){12, 8, 12, 8}),
              VV_STYLE(.bg = cur ? t->brand_subtle : (vv_Color){0}, .radius = vv_r(8),
                       .hover = &(vv_Style){ .bg = t->control_bg_hover }));
          vv_text(c, vv_fmt(c, "%d", i + 1),
                  VV_STYLE(.fg = cur ? t->accent : t->text_muted, .font_size = 13));
          VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 1), VV_STYLE(.bg = {0})) {
            vv_text(c, TRACKS[i].title,
                    VV_STYLE(.fg = cur ? t->text : t->text_secondary, .font_size = 14));
            vv_text(c, TRACKS[i].artist, VV_STYLE(.fg = t->text_muted, .font_size = 11));
          }
          char d[8];
          vv_text(c, mmss(d, TRACKS[i].dur), VV_STYLE(.fg = t->text_muted, .font_size = 12));
          vv_end_box(c);
          if (vv_clicked(c, row)) { a->track = i; a->pos = 0; a->playing = true; }
        }
      }
    }
  }
}

static void update(void *st, vv_Event e) {
  App *a = st;
  if (e.msg == M_SEEK) a->pos = (float)e.data.as_float;
  if (e.msg == M_VOL)  a->vol = (float)e.data.as_float;
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Now Playing", 520, 820);
  if (!app) return 1;
  for (const char *const *f = vv_default_font_paths(); *f; f++)
    if (vv_app_load_font(app, *f)) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);

  App a = { .app = app, .vec = vv_app_vector(app), .vol = 0.7f, .playing = true };
  a.play  = vv_vector_icon_svg(a.vec, SVG_PLAY,  (int)strlen(SVG_PLAY));
  a.pause = vv_vector_icon_svg(a.vec, SVG_PAUSE, (int)strlen(SVG_PAUSE));
  a.prev  = vv_vector_icon_svg(a.vec, SVG_PREV,  (int)strlen(SVG_PREV));
  a.next  = vv_vector_icon_svg(a.vec, SVG_NEXT,  (int)strlen(SVG_NEXT));
  a.heart = vv_vector_icon_svg(a.vec, SVG_HEART, (int)strlen(SVG_HEART));
  a.shuf  = vv_vector_icon_svg(a.vec, SVG_SHUF,  (int)strlen(SVG_SHUF));

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;

    if (a.playing) {
      a.pos += dt;
      a.spin += dt * 2.2f;
      if (a.pos >= TRACKS[a.track].dur) { a.track = (a.track + 1) % NTRACK; a.pos = 0; }
    }

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi); a.dpi = dpi;
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    redraw_art(&a, (int)(300 * dpi));
    vv_invalidate(&ctx);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &a);
    vv_app_frame_begin(app, vv_rgb(0.05f, 0.06f, 0.09f));
    if (cmds) vv_render(vv_app_backend(app), cmds, w, h, dpi);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    vv_app_frame_end(app);
  }
  if (a.art) vv_canvas_free(a.art);
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
