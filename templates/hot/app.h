// app.h — the contract shared by the host and the reloadable view module.
//
// State lives in the host process so it survives a reload; only the update/view
// *functions* live in the .so that gets swapped in. Put your state struct and
// message ids here; both sides include this file.
#pragma once
#include "verve/verve.h"

// Messages — 0 is reserved (VV_MSG_NONE), so start at 1.
enum { MSG_INC = 1, MSG_RESET };

typedef struct {
  int count;
} App;

// Exported by the reloadable module (view.c). The host looks these up by name.
void view_update(void *state, vv_Event ev);
void view_build(vv_Ctx *ctx, void *state);
