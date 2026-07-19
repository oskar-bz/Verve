// playground.c — a live GLSL shader playground: Verve UI chrome wrapped around a
// raw-OpenGL viewport. This is the graphics-app shape the library is designed
// for (§14.3): the animated document is drawn by the app on the GPU, and Verve
// supplies the reconciled, spring-animated controls around it.
//
// The bridge is vv_custom(): it places a leaf in the tree, laid out like any
// box, whose pixels the backend fills by calling our draw_canvas() with the
// node's on-screen rect (scissored, state restored afterward). Everything else —
// the panel, the sliders, their animation — is ordinary Verve.
//
//   drag the sliders  -> the shader responds (uniforms driven by app state)
//   move the mouse    -> warps the field (cursor fed in as a uniform)
// Build with `make gui`, run ./build/playground.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <stdio.h>

enum { MSG_SPEED = 1, MSG_SCALE, MSG_WARP, MSG_HUE, MSG_BRIGHT };

typedef struct {
  float speed, scale, warp, hue, bright; // shader params (the model)
  float fps;
  vv_Vec2 mouse;                         // window-space cursor, for the shader
  // GPU objects owned by the app, not by Verve.
  GLuint prog, vao;
  GLint u_time, u_speed, u_scale, u_warp, u_hue, u_bright, u_res, u_mouse;
  Uint64 t0, freq;
  vv_CustomDraw cb; // persistent handle handed to vv_custom()
} Playground;

static const char *VS =
    "#version 330 core\nout vec2 v_uv;\n"
    "void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
    " v_uv=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";

static const char *FS =
    "#version 330 core\n"
    "in vec2 v_uv; out vec4 frag;\n"
    "uniform float u_time,u_speed,u_scale,u_warp,u_hue,u_bright;\n"
    "uniform vec2 u_res,u_mouse;\n"
    "vec3 hsv2rgb(vec3 c){vec3 p=abs(fract(c.xxx+vec3(0.,2./3.,1./3.))*6.-3.);"
    " return c.z*mix(vec3(1.),clamp(p-1.,0.,1.),c.y);}\n"
    "void main(){\n"
    " vec2 uv=v_uv-0.5; uv.x*=u_res.x/u_res.y;\n"
    " vec2 mo=u_mouse-0.5;\n"
    " float t=u_time*u_speed;\n"
    " vec2 p=uv*u_scale+mo*2.0;\n"
    " float d=0.0;\n"
    " for(int i=0;i<6;i++){ p+=u_warp*vec2(sin(1.5*p.y+t),cos(1.5*p.x-t));"
    " d+=0.5*sin(p.x+0.7*p.y+t*1.3); }\n"
    " float v=0.5+0.5*sin(3.0*d+t);\n"
    " vec3 col=hsv2rgb(vec3(u_hue+0.08*d,0.75,pow(v,1.5)*u_bright));\n"
    " frag=vec4(col,1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); fprintf(stderr, "shader: %s\n", log); }
  return s;
}

// The vv_custom callback: raw GL, drawing a fullscreen triangle through the
// plasma shader into the node's rect. The backend has already scissored to it.
static void draw_canvas(void *ud, vv_Rect r) {
  Playground *pg = ud;
  float t = (float)(SDL_GetPerformanceCounter() - pg->t0) / (float)pg->freq;
  glUseProgram(pg->prog);
  glUniform1f(pg->u_time, t);
  glUniform1f(pg->u_speed, pg->speed);
  glUniform1f(pg->u_scale, pg->scale);
  glUniform1f(pg->u_warp, pg->warp);
  glUniform1f(pg->u_hue, pg->hue);
  glUniform1f(pg->u_bright, pg->bright);
  glUniform2f(pg->u_res, r.w, r.h);
  glUniform2f(pg->u_mouse, (pg->mouse.x - r.x) / r.w, (pg->mouse.y - r.y) / r.h);
  glBindVertexArray(pg->vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void update(void *st, vv_Event ev) {
  Playground *pg = st;
  float v = (float)ev.data.as_float;
  switch (ev.msg) {
  case MSG_SPEED:  pg->speed = v;  break;
  case MSG_SCALE:  pg->scale = v;  break;
  case MSG_WARP:   pg->warp = v;   break;
  case MSG_HUE:    pg->hue = v;    break;
  case MSG_BRIGHT: pg->bright = v; break;
  default: break;
  }
}

static void knob(vv_Ctx *c, const char *key, const char *name, float val,
                 float lo, float hi, vv_Msg msg) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 5),
         VV_STYLE(.bg = {0})) {
    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .main = VV_ALIGN_SPACE_BETWEEN),
           VV_STYLE(.bg = {0})) {
      vv_text(c, name, VV_STYLE(.fg = t->text, .font_size = 14));
      vv_text(c, vv_fmt(c, "%.2f", (double)val),
              VV_STYLE(.fg = t->text_muted, .font_size = 13));
    }
    vv_slider(c, key, val, lo, hi, msg);
  }
}

static void view(vv_Ctx *c, void *st) {
  Playground *pg = st;
  const vv_Theme *t = vv_theme();

  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.05f, 0.05f, 0.07f))) {

    // The GPU viewport: a leaf laid out like any box, drawn by draw_canvas().
    vv_custom(c, "canvas", 6, &pg->cb,
              VV_LAYOUT(.w = vv_grow(1), .h = vv_grow(1)));

    // Ordinary Verve chrome: a spring-animated control panel.
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(280), .h = vv_grow(1),
                        .padding = vv_all(20), .gap = 18),
           VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.14f),
                    .border_width = (vv_Edges){1, 0, 0, 0}, .border_color = t->border)) {
      vv_text(c, "Shader Playground", VV_STYLE(.fg = t->text, .font_size = 20));
      vv_text(c, "raw GL viewport · Verve controls",
              VV_STYLE(.fg = t->text_muted, .font_size = 12));
      knob(c, "sp", "Speed", pg->speed, 0.0f, 3.0f, MSG_SPEED);
      knob(c, "sc", "Scale", pg->scale, 1.0f, 8.0f, MSG_SCALE);
      knob(c, "wp", "Warp", pg->warp, 0.0f, 1.2f, MSG_WARP);
      knob(c, "hu", "Hue", pg->hue, 0.0f, 1.0f, MSG_HUE);
      knob(c, "br", "Bright", pg->bright, 0.3f, 1.6f, MSG_BRIGHT);
      VV_BOX(c, VV_LAYOUT(.h = vv_grow(1)), VV_STYLE(.bg = {0})) {}
      vv_text(c, vv_fmt(c, "%.0f fps", (double)pg->fps),
              VV_STYLE(.fg = t->text_muted, .font_size = 13));
    }
  }
}

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Shader Playground", 960, 600);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  // No idle mode: the shader animates every frame.

  Playground pg = {.speed = 1.0f, .scale = 3.2f, .warp = 0.55f, .hue = 0.6f, .bright = 1.0f};
  pg.cb = (vv_CustomDraw){.fn = draw_canvas, .ud = &pg};
  pg.t0 = SDL_GetPerformanceCounter();
  pg.freq = SDL_GetPerformanceFrequency();

  // Build the shader program + an empty VAO (the fullscreen triangle is
  // generated from gl_VertexID, so no vertex buffer is needed).
  GLuint v = compile(GL_VERTEX_SHADER, VS), f = compile(GL_FRAGMENT_SHADER, FS);
  pg.prog = glCreateProgram();
  glAttachShader(pg.prog, v); glAttachShader(pg.prog, f);
  glLinkProgram(pg.prog);
  glDeleteShader(v); glDeleteShader(f);
  glGenVertexArrays(1, &pg.vao);
  pg.u_time = glGetUniformLocation(pg.prog, "u_time");
  pg.u_speed = glGetUniformLocation(pg.prog, "u_speed");
  pg.u_scale = glGetUniformLocation(pg.prog, "u_scale");
  pg.u_warp = glGetUniformLocation(pg.prog, "u_warp");
  pg.u_hue = glGetUniformLocation(pg.prog, "u_hue");
  pg.u_bright = glGetUniformLocation(pg.prog, "u_bright");
  pg.u_res = glGetUniformLocation(pg.prog, "u_res");
  pg.u_mouse = glGetUniformLocation(pg.prog, "u_mouse");

  vv_Input in = {0};
  uint64_t prev = SDL_GetPerformanceCounter();
  while (vv_app_pump(app, &in)) {
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;
    pg.mouse = in.mouse;
    if (dt > 0) pg.fps = pg.fps * 0.9f + 0.1f / dt;

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);

    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &pg);
    if (cmds) {
      vv_app_frame_begin(app, vv_rgb(0.05f, 0.05f, 0.07f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }
  }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
