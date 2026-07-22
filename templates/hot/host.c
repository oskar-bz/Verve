// host.c — the hot-reload host. This file rarely changes: it owns the window and
// the App state, and hands both to vv_hot_run, which watches the view .so and
// swaps it in on every rebuild. All your iteration happens in view.c.
#include "app.h"
#include "vv_hot.h"

int main(void) {
  static App state = {0}; // lives here, so it survives every reload

  return vv_hot_run(&(vv_HotDesc){
      .title   = "Verve \xc2\xb7 Hot Template",
      .width   = 720,
      .height  = 480,
      .so_path = "build/tpl_hotview.so", // must match the Makefile output
      .state   = &state,
      // .update_sym / .view_sym default to "view_update" / "view_build".
  });
}
