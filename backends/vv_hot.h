// vv_hot.h — a turn-key host for hot-reloadable Verve apps (POSIX/dlopen).
//
// Prototyping loop: the *state* lives in your host process (so it survives a
// reload), while the *update/view functions* live in a shared object that the
// host dlopen's and swaps whenever the file changes on disk. Edit your view,
// rebuild just the .so, and the running window updates without losing state.
//
// Your reloadable module exports two symbols (names overridable in the desc):
//   void view_update(void *state, vv_Event ev);
//   void view_build (vv_Ctx *ctx, void *state);
//
// The host is one call:
//   vv_hot_run(&(vv_HotDesc){ .title = "App", .so_path = "build/view.so",
//                             .state = &state });
#ifndef VV_HOT_H
#define VV_HOT_H

#include "vv_sdl_gl.h"

typedef struct {
  const char        *title;         // window title (default "Verve · Hot Reload")
  int                width, height; // window size (default 900 x 640)
  vv_Color           clear;         // clear colour; a==0 => a dark default
  const char *const *fonts;         // NULL-terminated TTF paths; NULL => defaults
  const char        *so_path;       // required: path to the reloadable .so
  const char        *update_sym;    // exported update symbol (default "view_update")
  const char        *view_sym;      // exported view symbol   (default "view_build")
  void              *state;         // persists across reloads (owned by the host)
  bool               clipboard;     // bind the OS clipboard (for text editors)
} vv_HotDesc;

// Run until the window is closed. Returns 0 on success, non-zero on setup error
// (e.g. the .so failed to load on first try). Watches so_path's mtime and
// reloads when it changes.
int vv_hot_run(const vv_HotDesc *desc);

#endif // VV_HOT_H
