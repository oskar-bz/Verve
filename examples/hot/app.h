// app.h — state + messages shared by the host and the hot-reloaded view.
// State lives in the host so it survives a reload; the view/update code lives
// in a .so that gets swapped in when the file changes.
#pragma once
#include "verve/verve.h"

enum { MSG_STEP = 1, MSG_TOGGLE, MSG_RESET };

typedef struct {
  int64_t count;
  bool    subtract;
} App;

// Exported by the reloadable module (examples/hot/view.c).
void view_update(void *state, vv_Event ev);
void view_build(vv_Ctx *ctx, void *state);
