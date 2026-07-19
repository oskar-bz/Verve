#include "verve/verve.h"
#include "vv_test.h"
#define MSG_GO 7
static uint32_t g_btn;
static void view(vv_Ctx *c, void *st){ (void)st;
  VV_BOX(c, ((vv_LayoutDecl){.dir=VV_COLUMN,.padding=vv_all(10),.gap=8}), ((vv_Style){0})) {
    g_btn = vv_button(c, "b", "Go", MSG_GO, vv_pi(1));
  }
}
static void key_frame(vv_Ctx *c, vv_Key k){
  vv_Input in = {0};
  if (k) { in.keys[0] = (vv_KeyEvent){(uint16_t)k, 0, 0}; in.key_count = 1; }
  vv_begin_frame(c, 0.016f, &in); view(c, NULL); vv_end_frame(c);
}
int main(void){
  vv_Ctx ctx; vv_init(&ctx); vv_set_window(&ctx, 200, 200, 1.0f);
  key_frame(&ctx, VV_KEY_NONE);          // establish
  CHECK(!vv_focused(&ctx, g_btn));
  key_frame(&ctx, VV_KEY_TAB);           // Tab focuses the button
  CHECK(vv_focused(&ctx, g_btn));
  key_frame(&ctx, VV_KEY_ENTER);         // Enter activates it
  CHECK(vv_activated(&ctx, g_btn));
  vv_Event ev; int hits = 0;
  while (vv_poll_event(&ctx, &ev)) if (ev.msg == MSG_GO) hits++;
  CHECK(hits == 1);                      // exactly one activation
  vv_shutdown(&ctx);
  if (vv_test_fails){ printf("FAILED (%d)\n", vv_test_fails); return 1; }
  printf("  ok\n"); return 0;
}
