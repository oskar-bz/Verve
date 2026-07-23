// trivia.c — networking the Verve way, headless.
//
// Fetches one multiple-choice question from the Open Trivia Database
// (https://opentdb.com) and renders it through the ordinary update/view/message
// loop. This is the "blessed" integration shape from networking.md §3.2:
//
//   * update() starts the request and, later, consumes its completion message.
//   * view() declares the UI for whatever state we're in (loading/ok/error).
//   * the host loop calls vv_net_dispatch() BEFORE vv_run_frame(), so a finished
//     request arrives as a normal vv_Event — the worker never touches vv_Ctx.
//
// It links libverve.a + libverve-net.a + libcurl + yyjson. Build & run:
//   make trivia && ./build/trivia          (or: make run-net ... / ./build/trivia)
//
// No GPU here: a tiny text backend prints the command buffer so you can see the
// exact UI tree Verve would draw. That keeps the example honest about the data
// flow without pulling in SDL.
#define _POSIX_C_SOURCE 199309L  // nanosleep
#include "verve/verve.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

// ---- messages -------------------------------------------------------------

enum { MSG_FETCH = 1, MSG_RESULT };

// ---- application state ----------------------------------------------------
//
// The response is copied into plain fields here in update(); we do NOT hold on
// to the vv_NetResponse past release. State is the source of truth for view().
typedef struct {
    vv_Net *net;
    vv_NetRequest *pending;   // in-flight request, or NULL

    enum { S_IDLE, S_LOADING, S_OK, S_ERROR } phase;
    char category[128];
    char question[512];
    char answers[4][256];     // correct + up to 3 incorrect, in fetched order
    int  answer_count;
    int  correct_index;
    char error[256];
    bool quit;
} App;

static const char *TRIVIA_URL =
    "https://opentdb.com/api.php?amount=1&type=multiple";

// ---- tiny HTML-entity decoder --------------------------------------------
//
// The Open Trivia DB HTML-escapes its text ("Don&#039;t", "&quot;", ...).
// Decode the handful of entities it actually emits so the printed question is
// readable. Copies src→dst (dst must hold cap bytes incl. NUL).
static void html_unescape(char *dst, size_t cap, const char *src, size_t len) {
    static const struct { const char *ent; char ch; } tbl[] = {
        { "&quot;", '"' }, { "&#039;", '\'' }, { "&#39;", '\'' },
        { "&amp;", '&' },  { "&lt;", '<' },    { "&gt;", '>' },
        { "&eacute;", 'e' }, { "&rsquo;", '\'' }, { "&ldquo;", '"' },
        { "&rdquo;", '"' }, { "&ouml;", 'o' }, { "&uuml;", 'u' },
    };
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < cap; ) {
        if (src[i] == '&') {
            bool hit = false;
            for (size_t t = 0; t < sizeof tbl / sizeof tbl[0]; t++) {
                size_t el = strlen(tbl[t].ent);
                if (i + el <= len && memcmp(src + i, tbl[t].ent, el) == 0) {
                    dst[o++] = tbl[t].ch;
                    i += el;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        dst[o++] = src[i++];
    }
    dst[o] = '\0';
}

// Copy a JSON string value into a fixed buffer, HTML-unescaping it.
static void copy_json_str(char *dst, size_t cap, const vv_JsonValue *v) {
    const char *s = NULL; size_t n = 0;
    if (v && vv_json_string(v, &s, &n)) html_unescape(dst, cap, s, n);
    else                                dst[0] = '\0';
}

// ---- update: state transitions only ---------------------------------------

static void update(void *st, vv_Event ev) {
    App *a = st;
    switch (ev.msg) {
    case MSG_FETCH:
        if (a->pending) break;  // one at a time
        a->phase = S_LOADING;
        a->pending = vv_net_start(a->net, &(vv_NetSpec){
            .method = "GET",
            .url = TRIVIA_URL,
            .parse_json = true,
            .follow_redirects = true,
            .timeout_ms = 15000,
            .complete_msg = MSG_RESULT,
        });
        if (!a->pending) {
            a->phase = S_ERROR;
            snprintf(a->error, sizeof a->error, "could not start request");
        }
        break;

    case MSG_RESULT: {
        vv_NetRequest *r = ev.data.as_ptr;
        const vv_NetResponse *resp = vv_net_response(r);

        if (!vv_net_succeeded(r)) {
            a->phase = S_ERROR;
            snprintf(a->error, sizeof a->error, "%s (status %ld)",
                     resp && resp->error_message ? resp->error_message
                                                 : "request failed",
                     resp ? resp->status : 0);
        } else if (!resp->json) {
            a->phase = S_ERROR;
            snprintf(a->error, sizeof a->error, "response was not JSON");
        } else {
            // { "response_code": 0, "results": [ { ... } ] }
            const vv_JsonValue *root = vv_json_root(resp->json);
            const vv_JsonValue *results = vv_json_object_get(root, "results");
            const vv_JsonValue *q0 = vv_json_array_at(results, 0);
            if (!q0) {
                a->phase = S_ERROR;
                snprintf(a->error, sizeof a->error, "no results in response");
            } else {
                copy_json_str(a->category, sizeof a->category,
                              vv_json_object_get(q0, "category"));
                copy_json_str(a->question, sizeof a->question,
                              vv_json_object_get(q0, "question"));

                // Correct answer goes first; then each incorrect one.
                copy_json_str(a->answers[0], sizeof a->answers[0],
                              vv_json_object_get(q0, "correct_answer"));
                a->correct_index = 0;
                a->answer_count = 1;

                const vv_JsonValue *wrong = vv_json_object_get(q0, "incorrect_answers");
                size_t nwrong = vv_json_array_count(wrong);
                for (size_t i = 0; i < nwrong && a->answer_count < 4; i++)
                    copy_json_str(a->answers[a->answer_count++],
                                  sizeof a->answers[0], vv_json_array_at(wrong, i));

                a->phase = S_OK;
            }
        }

        // Done with the result: release it and drop our handle. Everything we
        // need is already copied into App.
        vv_net_request_release(r);
        a->pending = NULL;
        a->quit = true;  // headless demo: one question, then exit
        break;
    }
    }
}

// ---- view: declare UI for the current state -------------------------------

static void view(vv_Ctx *c, void *st) {
    App *a = st;
    vv_Style card = { .bg = vv_rgb(0.12f, 0.12f, 0.14f), .radius = vv_r(8) };
    vv_Style fg   = { .fg = vv_rgb(0.95f, 0.95f, 0.97f) };
    vv_Style dim  = { .fg = vv_rgb(0.55f, 0.60f, 0.68f) };
    vv_Style good = { .fg = vv_rgb(0.40f, 0.85f, 0.55f) };

    VV_BOX(c, ((vv_LayoutDecl){ .dir = VV_COLUMN, .gap = 8, .padding = vv_all(16) }), card) {
        vv_text(c, "VERVE TRIVIA", dim);
        switch (a->phase) {
        case S_IDLE:
            vv_text(c, "Idle.", dim);
            break;
        case S_LOADING:
            vv_text(c, "Loading question...", fg);
            break;
        case S_ERROR:
            vv_text(c, "Error:", fg);
            vv_text(c, a->error, dim);
            break;
        case S_OK:
            vv_text(c, a->category, dim);
            vv_text(c, a->question, fg);
            for (int i = 0; i < a->answer_count; i++)
                vv_text(c, a->answers[i], i == a->correct_index ? good : fg);
            break;
        }
    }
}

// ---- headless text backend: print the command buffer ----------------------

static void be_begin(void *c, int w, int h, float s) { (void)c;(void)w;(void)h;(void)s; }
static void be_end(void *c) { (void)c; }
static void be_rects(void *c, const vv_CmdRect *r, int n) { (void)c;(void)r;(void)n; }
static void be_text(void *c, const vv_CmdText *t, int n) {
    (void)c;
    for (int i = 0; i < n; i++)
        printf("  %.*s\n", (int)t[i].len, t[i].utf8);
}

static void msleep(unsigned ms) {
    nanosleep(&(struct timespec){ .tv_sec = ms/1000, .tv_nsec = (ms%1000)*1000000 }, NULL);
}

int main(void) {
    vv_Ctx ctx;
    vv_init(&ctx);
    vv_set_window(&ctx, 640, 480, 1.0f);
    vv_set_idle_mode(&ctx, true);  // only rebuild when something changes

    vv_Backend be = { .begin = be_begin, .end = be_end,
                      .draw_rects = be_rects, .draw_text = be_text };

    vv_NetConfig cfg = vv_net_config_default();
    App app = { .net = vv_net_create(&cfg) };
    if (!app.net) { fprintf(stderr, "vv_net_create failed\n"); return 1; }

    // Kick off the fetch through the normal message path.
    vv_emit(&ctx, MSG_FETCH, vv_pp(NULL));

    // Blessed host loop: dispatch completions, then run a frame. The bounded
    // poll interval stands in for a real host's event wait (networking.md §4.4);
    // a GUI host would instead install a wake_fn to break out of its event loop.
    // We only draw when the app's phase changes, so the output reads as a clean
    // state machine: LOADING -> OK/ERROR.
    vv_Input in = {0};
    int last_phase = -1;
    for (int frame = 0; frame < 2000; frame++) {
        vv_net_dispatch(app.net, &ctx);
        vv_CommandBuffer *cmds = vv_run_frame(&ctx, 0.016f, &in, update, view, &app);
        if (cmds && (int)app.phase != last_phase) {
            last_phase = (int)app.phase;
            vv_render(&be, cmds, (int)ctx.win_w, (int)ctx.win_h, ctx.dpi_scale);
            printf("\n");
        }
        if (app.quit) break;
        msleep(16);
    }

    // If we're exiting with a request still in flight (e.g. loop timed out),
    // release is non-blocking; destroy joins any remaining worker.
    if (app.pending) vv_net_request_release(app.pending);
    vv_net_destroy(app.net);
    vv_shutdown(&ctx);
    return app.phase == S_OK ? 0 : 1;
}
