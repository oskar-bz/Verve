// vv_sdl_gl.h — reference backend: SDL3 window + OpenGL 3.3 core.
//
// Implements vv_Backend (§15). Rects go through a single rounded-box SDF shader
// (§9.1); text through an stb_truetype atlas (§10.1). The app owns the window;
// the core stays backend-free (open question 2: app owns windows).
#ifndef VV_SDL_GL_H
#define VV_SDL_GL_H

#include "verve/verve.h"

typedef struct vv_App vv_App;

vv_App     *vv_app_create(const char *title, int w, int h);
void        vv_app_destroy(vv_App *app);

vv_Backend *vv_app_backend(vv_App *app);

// Pump SDL events into `in` (mouse position, button, wheel). Returns false when
// the user asked to quit. `dpi_scale` (nullable) receives the display scale.
bool        vv_app_pump(vv_App *app, vv_Input *in);

// Current drawable size in pixels.
void        vv_app_size(vv_App *app, int *w, int *h, float *dpi_scale);

// Load a TTF font from disk. The first font loaded is vv_FontID 0 (the default).
vv_FontID   vv_app_load_font(vv_App *app, const char *path);

// Clear the framebuffer and prepare for a frame's vv_render call.
void        vv_app_frame_begin(vv_App *app, vv_Color clear);
void        vv_app_frame_end(vv_App *app);  // swap buffers

// Measure callback to register with the context (vv_set_measure_fn).
vv_Vec2     vv_app_measure(void *ud, const char *s, int len,
                           vv_FontID font, float size, float wrap_width);

#endif // VV_SDL_GL_H
