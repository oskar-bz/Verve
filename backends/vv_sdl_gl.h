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

// Block up to `timeout_ms` for the next event without consuming it, then return.
// Call this on idle frames (when vv_run_frame returns NULL) so the loop sleeps
// instead of busy-spinning — that spin starves the compositor's frame cadence,
// which shows up as animations that only advance when you move the mouse.
void        vv_app_wait_event(vv_App *app, int timeout_ms);

// ---- multi-window (§ design open-question 2: the app owns windows) ---------
// Open a second window that shares the parent's GL context — so shaders, the
// glyph atlas, and loaded fonts are reused, only per-window VAO/scissor state is
// its own. Each window gets its own vv_Ctx (one context per window). Returns
// NULL on failure.
vv_App     *vv_app_open_child(vv_App *parent, const char *title, int w, int h);

// Multi-window event pump. Polls SDL once and routes every event to the owning
// window's internal input (by window ID), so windows don't steal each other's
// events the way parallel vv_app_pump calls would. Read a window's input with
// vv_app_input, and check vv_app_should_close to know when the user closed it.
// Returns the number of windows still open (0 => all closed, exit the loop).
int         vv_app_pump_all(void);
vv_Input   *vv_app_input(vv_App *app);
bool        vv_app_should_close(vv_App *app);

// ---- native file dialogs ---------------------------------------------------
// The OS file picker (via SDL3). Asynchronous: `cb` fires later, during a
// vv_app_pump/vv_app_pump_all call, with the chosen path — or NULL if the user
// cancelled. `filter_name`/`filter_pat` describe one filter (e.g. "Theme",
// "vvtheme;json"); pass NULL/NULL for "any file".
typedef void (*vv_FileCb)(void *ud, const char *path);
void        vv_app_open_file(vv_App *app, const char *filter_name,
                             const char *filter_pat, vv_FileCb cb, void *ud);
void        vv_app_save_file(vv_App *app, const char *filter_name,
                             const char *filter_pat, const char *default_name,
                             vv_FileCb cb, void *ud);

// Current drawable size in pixels.
void        vv_app_size(vv_App *app, int *w, int *h, float *dpi_scale);

// Load a TTF font from disk. The first font loaded is vv_FontID 0 (the default).
vv_FontID   vv_app_load_font(vv_App *app, const char *path);

// Upload RGBA8 pixels (row-major, w*h*4 bytes) and return a texture id to put in
// a vv_ImageRef for vv_image(). Destroy with vv_app_texture_destroy.
vv_TexID    vv_app_texture_from_rgba(vv_App *app, const void *rgba, int w, int h);
void        vv_app_texture_destroy(vv_App *app, vv_TexID tex);

// Clear the framebuffer and prepare for a frame's vv_render call.
void        vv_app_frame_begin(vv_App *app, vv_Color clear);
void        vv_app_frame_end(vv_App *app);  // swap buffers

// Measure callback to register with the context (vv_set_measure_fn).
vv_Vec2     vv_app_measure(void *ud, const char *s, int len,
                           vv_FontID font, float size, float wrap_width);

// Wire the OS clipboard into a context so the editors' copy/cut/paste work.
void        vv_app_bind_clipboard(vv_App *app, vv_Ctx *ctx);

// Apply a cursor shape to the window. Call once per frame with vv_cursor(ctx).
void        vv_app_set_cursor(vv_App *app, vv_CursorShape shape);

#endif // VV_SDL_GL_H
