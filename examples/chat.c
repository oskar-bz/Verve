// chat.c — a multi-window chat client, and the argument that the message/
// update/view split scales. The whole app is ONE model (App), ONE update()
// with a flat switch, and a handful of *pure* view functions. Every window —
// the main workspace and any number of detached channel windows — is just
// another view over that single model, so there is no per-window logic, no
// syncing, no observers: post a message in one window and every window that
// shows that channel re-renders from the same state.
//
//   • Multi-window  — Detach pops a channel into its own OS window.
//   • Shared model  — one store of channels + messages; a revision counter
//     rebuilds the detached windows when the store changes (§ idle rebuilds).
//   • Live updates  — sending a message triggers a canned reply, so you can
//     watch a detached window light up while you type in the main one.
#include "verve/verve.h"
#include "vv_sdl_gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

enum {
  MSG_SELECT = 1, // .as_int = channel index (main window's active channel)
  MSG_SEND,       // .as_int = channel index (send that channel's draft)
  MSG_DETACH,     // .as_int = channel index (open a window pinned to it)
  MSG_ADD,        // create a new channel
  MSG_TOGGLE_BAR, // show/hide the sidebar
  MSG_DRAFT,      // no-op: the composer edits channel->draft in place
};

#define MAXCH  10
#define MAXMSG 120
#define MAXWIN 3

typedef struct { char who[20]; char text[200]; bool me; } Msg;
typedef struct {
  char name[24];
  bool dm;               // direct message vs. #channel
  Msg  msg[MAXMSG];
  int  n;
  int  unread;
  char draft[200];
} Channel;

typedef struct {
  Channel  ch[MAXCH];
  int      nch;
  int      cur;          // main window's selected channel
  int      view_channel; // transient: channel the window being built shows
  unsigned rev;          // bumped on any store change -> detached windows rebuild
  bool     sidebar;
  int      want_detach;  // channel to detach on the next pump, -1 = none
  vv_Ctx  *ctx;
  vv_App  *app;
} App;

// ---- model mutation (the only place state changes, all via update) ----------

static void post(App *a, int ci, const char *who, const char *text, bool me) {
  Channel *c = &a->ch[ci];
  if (c->n >= MAXMSG) { // ring: drop the oldest
    memmove(c->msg, c->msg + 1, sizeof(Msg) * (MAXMSG - 1));
    c->n--;
  }
  Msg *m = &c->msg[c->n++];
  snprintf(m->who, sizeof m->who, "%s", who);
  snprintf(m->text, sizeof m->text, "%s", text);
  m->me = me;
  if (!me && ci != a->cur) c->unread++;
  a->rev++;
}

// A deliberately dumb autoresponder — enough to show a reply arriving live in
// whatever window is showing that channel.
static const char *canned(const char *in) {
  size_t n = strlen(in);
  if (strchr(in, '?')) return "Good question — let me check and get back to you.";
  if (n && (in[n - 1] == '!'))     return "Haha, absolutely.";
  if (strstr(in, "hi") || strstr(in, "hey") || strstr(in, "hello"))
    return "Hey! What's up?";
  if (n < 8) return "Got it.";
  return "Makes sense to me. Anything else?";
}

static void send_draft(App *a, int ci) {
  Channel *c = &a->ch[ci];
  if (!c->draft[0]) return;
  char buf[200];
  snprintf(buf, sizeof buf, "%s", c->draft);
  c->draft[0] = 0;
  post(a, ci, "You", buf, true);
  post(a, ci, c->dm ? c->name : "verve-bot", canned(buf), false);
}

static void update(void *st, vv_Event ev) {
  App *a = st;
  switch (ev.msg) {
  case MSG_SELECT:
    a->cur = ev.data.as_int;
    a->ch[a->cur].unread = 0;
    a->rev++;
    break;
  case MSG_SEND:     send_draft(a, ev.data.as_int); break;
  case MSG_DETACH:   a->want_detach = ev.data.as_int; break;
  case MSG_TOGGLE_BAR: a->sidebar = !a->sidebar; a->rev++; break;
  case MSG_ADD:
    if (a->nch < MAXCH) {
      Channel *c = &a->ch[a->nch];
      snprintf(c->name, sizeof c->name, "channel-%d", a->nch + 1);
      c->dm = false; c->n = 0; c->unread = 0; c->draft[0] = 0;
      a->cur = a->nch++;
      a->rev++;
    }
    break;
  case MSG_DRAFT: break; // text_field edits the draft buffer in place
  }
}

// ---- pure views (read the model + vv_theme(), emit messages, mutate nothing) -

static vv_Color avatar_color(const char *name) {
  // Stable per-name hue from a tiny palette — no state, pure of the name.
  static const vv_Color pal[] = {
    {0.36f, 0.55f, 0.96f, 1}, {0.60f, 0.42f, 0.93f, 1},
    {0.94f, 0.46f, 0.42f, 1}, {0.29f, 0.72f, 0.55f, 1},
    {0.93f, 0.66f, 0.28f, 1}, {0.40f, 0.70f, 0.86f, 1},
  };
  unsigned h = 0;
  for (const char *p = name; *p; p++) h = h * 131 + (unsigned char)*p;
  return pal[h % (sizeof pal / sizeof pal[0])];
}

static void avatar(vv_Ctx *c, const char *name, float d) {
  const vv_Theme *t = vv_theme();
  char initial[2] = { name[0] ? (char)(name[0] & ~0x20) : '?', 0 };
  VV_BOX(c, VV_LAYOUT(.w = vv_fixed(d), .h = vv_fixed(d),
                      .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
         VV_STYLE(.bg = avatar_color(name), .radius = vv_r(d * 0.5f))) {
    vv_text(c, initial, VV_STYLE(.fg = t->on_accent, .font_size = d * 0.42f));
  }
  (void)t;
}

static void bubble(vv_Ctx *c, const char *key, const Msg *m) {
  const vv_Theme *t = vv_theme();
  // Own messages hug the right in accent; others hug the left in a surface tone.
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .gap = 8,
                      .main = m->me ? VV_ALIGN_END : VV_ALIGN_START),
         VV_STYLE(.bg = {0})) {
    if (!m->me) avatar(c, m->who, 30);
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = {VV_SIZE_FIT, 0, 0, 420}, .gap = 3,
                        .padding = vv_hv(12, 8)),
           VV_STYLE(.bg = m->me ? t->accent : t->surface_hi, .radius = vv_r(12))) {
      if (!m->me)
        vv_text(c, m->who, VV_STYLE(.fg = avatar_color(m->who),
                                    .font_size = t->font_size - 3));
      vv_text(c, m->text, VV_STYLE(.fg = m->me ? t->on_accent : t->text,
                                   .font_size = t->font_size));
    }
    if (m->me) avatar(c, "You", 30);
  }
  (void)key;
}

static void message_list(vv_Ctx *c, App *a, int ci) {
  const vv_Theme *t = vv_theme();
  Channel *ch = &a->ch[ci];
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1), .gap = 10,
                      .padding = vv_all(16), .scroll_y = true, .clip = true),
         VV_STYLE(.bg = t->surface)) {
    if (ch->n == 0)
      vv_text(c, "No messages yet — say hi.",
              VV_STYLE(.fg = t->text_muted, .font_size = t->font_size));
    for (int i = 0; i < ch->n; i++)
      bubble(c, vv_fmt(c, "m%d", i), &ch->msg[i]);
  }
}

static void composer(vv_Ctx *c, App *a, int ci) {
  const vv_Theme *t = vv_theme();
  Channel *ch = &a->ch[ci];
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                      .gap = 10, .padding = vv_all(12)),
         VV_STYLE(.bg = t->surface, .border_width = (vv_Edges){1, 0, 0, 0},
                  .border_color = t->border)) {
    VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {
      uint32_t f = vv_text_field(c, "draft", ch->draft, (int)sizeof ch->draft,
                                 vv_fmt(c, "Message %s%s", ch->dm ? "" : "#", ch->name),
                                 MSG_DRAFT);
      if (vv_activated(c, f)) vv_emit(c, MSG_SEND, vv_pi(ci)); // Enter sends
    }
    vv_button(c, "send", "Send", MSG_SEND, vv_pi(ci));
  }
}

static void channel_header(vv_Ctx *c, App *a, int ci, bool detachable) {
  const vv_Theme *t = vv_theme();
  Channel *ch = &a->ch[ci];
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_fixed(52),
                      .cross = VV_ALIGN_CENTER, .gap = 10, .padding = vv_hv(16, 0)),
         VV_STYLE(.bg = t->surface, .border_width = (vv_Edges){0, 0, 1, 0},
                  .border_color = t->border)) {
    avatar(c, ch->name, 30);
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .gap = 1), VV_STYLE(.bg = {0})) {
      vv_text(c, vv_fmt(c, "%s%s", ch->dm ? "" : "#", ch->name),
              VV_STYLE(.fg = t->text, .font_size = t->font_size + 2));
      vv_text(c, vv_fmt(c, "%d message%s", ch->n, ch->n == 1 ? "" : "s"),
              VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 3));
    }
    VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
    if (detachable) vv_button(c, "detach", "Detach", MSG_DETACH, vv_pi(ci));
  }
}

static void channel_row(vv_Ctx *c, App *a, int i) {
  const vv_Theme *t = vv_theme();
  Channel *ch = &a->ch[i];
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER, .gap = 8),
         VV_STYLE(.bg = {0})) {
    VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {
      vv_list_item(c, vv_fmt(c, "ch%d", i),
                   vv_fmt(c, "%s%s", ch->dm ? "\xe2\x97\x8f " : "# ", ch->name),
                   i == a->cur, MSG_SELECT, vv_pi(i));
    }
    if (ch->unread > 0)
      VV_BOX(c, VV_LAYOUT(.w = vv_fixed(22), .h = vv_fixed(20),
                          .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER),
             VV_STYLE(.bg = t->accent, .radius = vv_r(10))) {
        vv_text(c, vv_fmt(c, "%d", ch->unread),
                VV_STYLE(.fg = t->on_accent, .font_size = t->font_size - 4));
      }
  }
}

static void sidebar(vv_Ctx *c, App *a) {
  const vv_Theme *t = vv_theme();
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(230), .h = vv_grow(1),
                      .gap = 4, .padding = vv_all(12), .scroll_y = true, .clip = true),
         VV_STYLE(.bg = vv_rgb(0.10f, 0.11f, 0.14f),
                  .border_width = (vv_Edges){0, 1, 0, 0}, .border_color = t->border)) {
    vv_text(c, "Verve Chat", VV_STYLE(.fg = t->text, .font_size = t->font_size + 6));
    vv_text(c, "CHANNELS", VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 4));
    for (int i = 0; i < a->nch; i++) if (!a->ch[i].dm) channel_row(c, a, i);
    vv_text(c, "DIRECT MESSAGES",
            VV_STYLE(.fg = t->text_muted, .font_size = t->font_size - 4));
    for (int i = 0; i < a->nch; i++) if (a->ch[i].dm) channel_row(c, a, i);
    VV_BOX(c, VV_LAYOUT(.h = vv_fixed(8)), VV_STYLE(.bg = {0})) {}
    vv_button(c, "add", "+ New channel", MSG_ADD, VV_NO_PAYLOAD);
  }
}

// The main workspace: sidebar + the active channel's conversation.
static void view(vv_Ctx *c, void *st) {
  App *a = st;
  const vv_Theme *t = vv_theme();
  int ci = a->cur;
  VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.08f, 0.09f, 0.11f))) {
    if (a->sidebar) sidebar(c, a);
    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
           VV_STYLE(.bg = {0})) {
      VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                          .gap = 6, .padding = vv_hv(8, 6)),
             VV_STYLE(.bg = t->surface)) {
        vv_button(c, "bar", a->sidebar ? "\xc2\xab" : "\xc2\xbb", MSG_TOGGLE_BAR,
                  VV_NO_PAYLOAD);
        channel_header(c, a, ci, true);
      }
      message_list(c, a, ci);
      composer(c, a, ci);
    }
  }
  (void)t;
}

// A detached window: the same conversation views, pinned to one channel, no
// sidebar. Identical building blocks — only the composition differs.
static void view_detached(vv_Ctx *c, void *st) {
  App *a = st;
  int ci = a->view_channel;
  VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1)),
         VV_STYLE(.bg = vv_rgb(0.08f, 0.09f, 0.11f))) {
    channel_header(c, a, ci, false);
    message_list(c, a, ci);
    composer(c, a, ci);
  }
}

static void seed(App *a) {
  const char *chans[] = {"general", "engineering", "design"};
  const char *dms[]   = {"Ada", "Grace", "Linus"};
  for (unsigned i = 0; i < sizeof chans / sizeof chans[0]; i++) {
    Channel *c = &a->ch[a->nch++];
    snprintf(c->name, sizeof c->name, "%s", chans[i]);
  }
  for (unsigned i = 0; i < sizeof dms / sizeof dms[0]; i++) {
    Channel *c = &a->ch[a->nch++];
    snprintf(c->name, sizeof c->name, "%s", dms[i]); c->dm = true;
  }
  post(a, 0, "verve-bot", "Welcome to Verve Chat! Pick a channel and say hi.", false);
  post(a, 0, "Ada", "Hit Detach to pop a channel into its own window.", false);
  post(a, 1, "Grace", "The build is green again \xe2\x9c\x93", false);
  a->ch[1].unread = 1;
  post(a, 3, "Ada", "Are we still on for the review?", false);
  a->ch[3].unread = 1;
}

typedef struct { vv_App *app; vv_Ctx ctx; bool used; int channel; unsigned seen; } Win;

int main(void) {
  vv_App *app = vv_app_create("Verve \xc2\xb7 Chat", 1040, 680);
  if (!app) return 1;
  const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", NULL};
  for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

  vv_Ctx ctx; vv_init(&ctx);
  vv_set_measure_fn(&ctx, vv_app_measure, app);
  vv_set_idle_mode(&ctx, true);
  vv_app_bind_clipboard(app, &ctx);

  static App state;
  state.sidebar = true;
  state.want_detach = -1;
  state.app = app; state.ctx = &ctx;
  seed(&state);

  Win wins[MAXWIN] = {0};
  unsigned main_seen = state.rev;

  while (vv_app_pump_all() > 0) {
    bool drew = false;
    static uint64_t prev; if (!prev) prev = SDL_GetPerformanceCounter();
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency(); prev = now;
    if (dt > 0.1f) dt = 0.1f;
    if (vv_app_should_close(app)) break;

    // A detached window mutated the shared store? Rebuild the main view too.
    if (main_seen != state.rev) { vv_invalidate(&ctx); main_seen = state.rev; }

    int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
    vv_set_window(&ctx, (float)w, (float)h, dpi);
    state.view_channel = state.cur;
    vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, vv_app_input(app), update, view, &state);
    vv_app_set_cursor(app, vv_cursor(&ctx));
    if (cmds) {
      drew = true;
      vv_app_frame_begin(app, vv_rgb(0.08f, 0.09f, 0.11f));
      vv_render(vv_app_backend(app), cmds, w, h, dpi);
      vv_app_frame_end(app);
    }

    // Detach request -> open a window pinned to that channel.
    if (state.want_detach >= 0) {
      for (int i = 0; i < MAXWIN; i++) {
        if (wins[i].used) continue;
        wins[i].app = vv_app_open_child(app, vv_fmt(&ctx, "Verve \xc2\xb7 %s",
                                                     state.ch[state.want_detach].name), 460, 620);
        if (!wins[i].app) break;
        vv_init(&wins[i].ctx);
        vv_set_measure_fn(&wins[i].ctx, vv_app_measure, wins[i].app);
        vv_set_idle_mode(&wins[i].ctx, true);
        vv_app_bind_clipboard(wins[i].app, &wins[i].ctx);
        wins[i].channel = state.want_detach;
        wins[i].seen = state.rev;
        wins[i].used = true;
        break;
      }
      state.want_detach = -1;
    }

    for (int i = 0; i < MAXWIN; i++) {
      if (!wins[i].used) continue;
      if (vv_app_should_close(wins[i].app)) {
        vv_shutdown(&wins[i].ctx); vv_app_destroy(wins[i].app);
        wins[i].used = false; continue;
      }
      // The store changed in another window? Rebuild this one (idle-safe).
      if (wins[i].seen != state.rev) {
        vv_invalidate(&wins[i].ctx);
        wins[i].seen = state.rev;
      }
      int cw, chh; float cdpi; vv_app_size(wins[i].app, &cw, &chh, &cdpi);
      vv_set_window(&wins[i].ctx, (float)cw, (float)chh, cdpi);
      state.view_channel = wins[i].channel;
      vv_CommandBuffer *cc = vv_run_frame(&wins[i].ctx, dt, vv_app_input(wins[i].app),
                                          update, view_detached, &state);
      vv_app_set_cursor(wins[i].app, vv_cursor(&wins[i].ctx));
      if (cc) {
        drew = true;
        vv_app_frame_begin(wins[i].app, vv_rgb(0.08f, 0.09f, 0.11f));
        vv_render(vv_app_backend(wins[i].app), cc, cw, chh, cdpi);
        vv_app_frame_end(wins[i].app);
      }
    }

    if (!drew) vv_app_wait_event(app, 16);
  }

  for (int i = 0; i < MAXWIN; i++)
    if (wins[i].used) { vv_shutdown(&wins[i].ctx); vv_app_destroy(wins[i].app); }
  vv_shutdown(&ctx);
  vv_app_destroy(app);
  return 0;
}
