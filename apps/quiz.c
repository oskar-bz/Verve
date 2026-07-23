// quiz.c — a real trivia game built on Verve + the optional networking module.
//
// Fetches a round of multiple-choice questions from the Open Trivia Database
// (https://opentdb.com) over HTTPS, shuffles the answers, and plays them one at
// a time: pick an answer, see it marked right/wrong, advance, and get a final
// score. It is the GUI counterpart to examples/trivia.c and shows the blessed
// networking shape from networking.md §3.2 inside a windowed app:
//
//   * update() is the only place state changes. It starts the request and, when
//     the completion message arrives, folds the parsed data into state.
//   * view() is a pure function of state — one branch per phase.
//   * the host loop calls vv_net_dispatch(net, &ctx) BEFORE vv_run_frame(), so a
//     finished request lands as an ordinary vv_Event. The worker thread never
//     touches vv_Ctx.
//
// Build & run:  make run APP=quiz        (links libverve + libverve-net + curl + yyjson)
#include "vv_sdl_gl.h"
#include "verve/vv_net.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const vv_Theme *TH;

// ---- messages -------------------------------------------------------------
enum {
    MSG_START = 1,  // begin / restart a round (fetch questions)
    MSG_RESULT,     // network completion (payload = vv_NetRequest*)
    MSG_ANSWER,     // player picked an answer (payload = answer index)
    MSG_NEXT,       // advance to the next question
};

// ---- model ----------------------------------------------------------------
#define MAX_Q      10
#define MAX_ANS     4
#define ANS_CAP   224
#define Q_CAP     600

typedef struct {
    char category[128];
    char difficulty[24];
    char question[Q_CAP];
    char answers[MAX_ANS][ANS_CAP];
    int  answer_count;
    int  correct;            // index into answers[] after shuffling
} Question;

typedef enum { PH_MENU, PH_LOADING, PH_PLAY, PH_ANSWERED, PH_DONE, PH_ERROR } Phase;

typedef struct {
    vv_Net        *net;
    vv_Ctx        *ctx;
    vv_NetRequest *pending;   // in-flight request, or NULL

    Phase     phase;
    Question  q[MAX_Q];
    int       count;          // questions loaded this round
    int       index;          // current question
    int       score;
    int       picked;         // answer chosen for the current question, or -1
    char      error[256];
} Game;

static const char *QUIZ_URL =
    "https://opentdb.com/api.php?amount=10&type=multiple";

// ---- text helpers ---------------------------------------------------------

// The Open Trivia DB HTML-escapes its strings ("Don&#039;t", "&quot;", ...).
// Decode the handful of entities it emits so questions read naturally.
static void html_unescape(char *dst, size_t cap, const char *src, size_t len) {
    static const struct { const char *ent; const char *rep; } tbl[] = {
        { "&quot;", "\"" }, { "&#039;", "'" }, { "&#39;", "'" }, { "&amp;", "&" },
        { "&lt;", "<" }, { "&gt;", ">" }, { "&rsquo;", "'" }, { "&lsquo;", "'" },
        { "&ldquo;", "\"" }, { "&rdquo;", "\"" }, { "&hellip;", "..." },
        { "&eacute;", "\xc3\xa9" }, { "&egrave;", "\xc3\xa8" },
        { "&ouml;", "\xc3\xb6" }, { "&uuml;", "\xc3\xbc" }, { "&auml;", "\xc3\xa4" },
        { "&ntilde;", "\xc3\xb1" }, { "&deg;", "\xc2\xb0" }, { "&pi;", "\xcf\x80" },
    };
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < cap; ) {
        if (src[i] == '&') {
            bool hit = false;
            for (size_t t = 0; t < sizeof tbl / sizeof tbl[0]; t++) {
                size_t el = strlen(tbl[t].ent);
                if (i + el <= len && memcmp(src + i, tbl[t].ent, el) == 0) {
                    for (const char *p = tbl[t].rep; *p && o + 1 < cap; p++) dst[o++] = *p;
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

static void copy_json_str(char *dst, size_t cap, const vv_JsonValue *v) {
    const char *s = NULL; size_t n = 0;
    if (v && vv_json_string(v, &s, &n)) html_unescape(dst, cap, s, n);
    else                                dst[0] = '\0';
}

// Fisher-Yates shuffle of a question's answers, keeping `correct` in sync.
static void shuffle_answers(Question *q) {
    for (int i = q->answer_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        char tmp[ANS_CAP];
        memcpy(tmp, q->answers[i], ANS_CAP);
        memcpy(q->answers[i], q->answers[j], ANS_CAP);
        memcpy(q->answers[j], tmp, ANS_CAP);
        if      (q->correct == i) q->correct = j;
        else if (q->correct == j) q->correct = i;
    }
}

// Parse the opentdb payload into g->q[]. Returns false (with g->error set) on a
// malformed or empty response.
static bool parse_questions(Game *g, const vv_JsonDoc *doc) {
    const vv_JsonValue *root = vv_json_root(doc);
    const vv_JsonValue *code = vv_json_object_get(root, "response_code");
    double rc = -1;
    if (!vv_json_number(code, &rc) || rc != 0) {
        snprintf(g->error, sizeof g->error,
                 "the trivia service returned no questions (code %d)", (int)rc);
        return false;
    }
    const vv_JsonValue *results = vv_json_object_get(root, "results");
    size_t n = vv_json_array_count(results);
    if (n == 0) { snprintf(g->error, sizeof g->error, "empty question set"); return false; }
    if (n > MAX_Q) n = MAX_Q;

    for (size_t i = 0; i < n; i++) {
        const vv_JsonValue *item = vv_json_array_at(results, i);
        Question *q = &g->q[i];
        memset(q, 0, sizeof *q);
        copy_json_str(q->category, sizeof q->category, vv_json_object_get(item, "category"));
        copy_json_str(q->difficulty, sizeof q->difficulty, vv_json_object_get(item, "difficulty"));
        copy_json_str(q->question, sizeof q->question, vv_json_object_get(item, "question"));

        // Correct answer first, then the incorrect ones; then shuffle.
        copy_json_str(q->answers[0], ANS_CAP, vv_json_object_get(item, "correct_answer"));
        q->correct = 0;
        q->answer_count = 1;
        const vv_JsonValue *wrong = vv_json_object_get(item, "incorrect_answers");
        size_t nw = vv_json_array_count(wrong);
        for (size_t k = 0; k < nw && q->answer_count < MAX_ANS; k++)
            copy_json_str(q->answers[q->answer_count++], ANS_CAP, vv_json_array_at(wrong, k));
        shuffle_answers(q);
    }
    g->count = (int)n;
    return true;
}

// ---- update: the only place state changes ---------------------------------

static void update(void *state, vv_Event ev) {
    Game *g = state;
    switch (ev.msg) {
    case MSG_START:
        if (g->pending) break;                 // a fetch is already running
        g->score = g->index = 0;
        g->picked = -1;
        g->phase = PH_LOADING;
        g->pending = vv_net_start(g->net, &(vv_NetSpec){
            .method = "GET",
            .url = QUIZ_URL,
            .parse_json = true,
            .follow_redirects = true,
            .timeout_ms = 15000,
            .complete_msg = MSG_RESULT,
        });
        if (!g->pending) {
            g->phase = PH_ERROR;
            snprintf(g->error, sizeof g->error, "could not start the request");
        }
        break;

    case MSG_RESULT: {
        vv_NetRequest *r = ev.data.as_ptr;
        const vv_NetResponse *resp = vv_net_response(r);
        if (!vv_net_succeeded(r)) {
            g->phase = PH_ERROR;
            snprintf(g->error, sizeof g->error, "%s",
                     resp && resp->error_message ? resp->error_message
                                                 : "network request failed");
        } else if (!resp->json || !parse_questions(g, resp->json)) {
            g->phase = PH_ERROR;                // parse_questions set g->error
            if (!resp->json) snprintf(g->error, sizeof g->error, "response was not JSON");
        } else {
            g->index = 0;
            g->picked = -1;
            g->phase = PH_PLAY;
        }
        vv_net_request_release(r);              // everything needed is copied
        g->pending = NULL;
        break;
    }

    case MSG_ANSWER:
        if (g->phase != PH_PLAY) break;         // ignore clicks after answering
        g->picked = (int)ev.data.as_int;
        if (g->picked == g->q[g->index].correct) g->score++;
        g->phase = PH_ANSWERED;
        break;

    case MSG_NEXT:
        if (g->phase != PH_ANSWERED) break;
        g->picked = -1;
        if (++g->index >= g->count) g->phase = PH_DONE;
        else                        g->phase = PH_PLAY;
        break;
    }
}

// ---- view helpers ---------------------------------------------------------

static void answer_button(vv_Ctx *c, int i, const char *text) {
    char key[8]; snprintf(key, sizeof key, "a%d", i);
    vv_button(c, key, vv_fmt(c, "%c.  %s", 'A' + i, text), MSG_ANSWER, vv_pi(i));
}

// A colored, non-interactive answer card shown after the player has answered.
static void answer_result(vv_Ctx *c, Game *g, int i, const char *text) {
    const Question *q = &g->q[g->index];
    bool is_correct = (i == q->correct);
    bool is_picked  = (i == g->picked);
    vv_Color bg = TH->control_bg_rest, fg = TH->text_secondary;
    if (is_correct)      { bg = TH->status_success; fg = TH->text_on_brand; }
    else if (is_picked)  { bg = TH->status_error;   fg = TH->text_on_brand; }

    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER,
                        .padding = vv_hv(14, 12), .gap = 6),
           VV_STYLE(.bg = bg, .radius = vv_r(TH->radius_md))) {
        vv_text(c, vv_fmt(c, "%c.  %s", 'A' + i, text),
                VV_STYLE(.fg = fg, .font_size = TH->font_size));
    }
}

// ---- view: pure function of state -----------------------------------------

static void view(vv_Ctx *c, void *state) {
    Game *g = state;

    VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .h = vv_grow(1),
                        .main = VV_ALIGN_CENTER, .cross = VV_ALIGN_CENTER,
                        .padding = vv_all(24)),
           VV_STYLE(.bg = TH->surface_app)) {

        // The card: a fixed-width column so long text wraps predictably.
        VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_fixed(600), .gap = 16,
                            .padding = vv_all(28)),
               VV_STYLE(.bg = TH->surface_card, .radius = vv_r(TH->radius_lg))) {

            vv_text(c, "VERVE TRIVIA",
                    VV_STYLE(.fg = TH->brand_primary, .font_size = TH->font_size + 2));

            switch (g->phase) {
            case PH_MENU:
                vv_text(c, "Ten multiple-choice questions, pulled live from the",
                        VV_STYLE(.fg = TH->text_secondary));
                vv_text(c, "Open Trivia Database. How many can you get?",
                        VV_STYLE(.fg = TH->text_secondary));
                VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .main = VV_ALIGN_CENTER, .w = vv_grow(1)),
                       VV_STYLE(.bg = {0})) {
                    vv_button(c, "start", "Start quiz", MSG_START, VV_NO_PAYLOAD);
                }
                break;

            case PH_LOADING:
                vv_text(c, "Fetching questions\xe2\x80\xa6", VV_STYLE(.fg = TH->text_primary));
                vv_progress(c, "load", g->pending ? vv_net_progress(g->pending) : 0.0f);
                break;

            case PH_ERROR:
                vv_text(c, "Couldn't load the quiz",
                        VV_STYLE(.fg = TH->status_error, .font_size = TH->font_size + 2));
                vv_text(c, g->error, VV_STYLE(.fg = TH->text_muted));
                vv_button(c, "retry", "Try again", MSG_START, VV_NO_PAYLOAD);
                break;

            case PH_PLAY:
            case PH_ANSWERED: {
                const Question *q = &g->q[g->index];
                // Header: category + progress + score.
                VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER),
                       VV_STYLE(.bg = {0})) {
                    vv_text(c, vv_fmt(c, "%s \xc2\xb7 %s", q->category, q->difficulty),
                            VV_STYLE(.fg = TH->text_muted, .font_size = TH->font_size - 1));
                    VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
                    vv_text(c, vv_fmt(c, "%d / %d", g->index + 1, g->count),
                            VV_STYLE(.fg = TH->text_muted, .font_size = TH->font_size - 1));
                }
                vv_text(c, q->question,
                        VV_STYLE(.fg = TH->text_primary, .font_size = TH->font_size + 6));

                VV_BOX(c, VV_LAYOUT(.dir = VV_COLUMN, .w = vv_grow(1), .gap = 8),
                       VV_STYLE(.bg = {0})) {
                    for (int i = 0; i < q->answer_count; i++) {
                        if (g->phase == PH_PLAY) answer_button(c, i, q->answers[i]);
                        else                     answer_result(c, g, i, q->answers[i]);
                    }
                }

                if (g->phase == PH_ANSWERED) {
                    VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .w = vv_grow(1), .cross = VV_ALIGN_CENTER),
                           VV_STYLE(.bg = {0})) {
                        bool right = g->picked == q->correct;
                        vv_text(c, right ? "Correct!" : "Not quite.",
                                VV_STYLE(.fg = right ? TH->status_success : TH->status_error));
                        VV_BOX(c, VV_LAYOUT(.w = vv_grow(1)), VV_STYLE(.bg = {0})) {}
                        vv_text(c, vv_fmt(c, "Score %d", g->score),
                                VV_STYLE(.fg = TH->text_secondary));
                        vv_button(c, "next",
                                  g->index + 1 >= g->count ? "See results" : "Next question",
                                  MSG_NEXT, VV_NO_PAYLOAD);
                    }
                }
                break;
            }

            case PH_DONE: {
                int pct = g->count ? (g->score * 100 / g->count) : 0;
                const char *grade = pct >= 80 ? "Brilliant!" :
                                    pct >= 50 ? "Not bad."   : "Better luck next time.";
                vv_text(c, vv_fmt(c, "You scored %d / %d", g->score, g->count),
                        VV_STYLE(.fg = TH->text_primary, .font_size = TH->font_size + 12));
                vv_text(c, vv_fmt(c, "%s  (%d%%)", grade, pct),
                        VV_STYLE(.fg = TH->text_secondary));
                VV_BOX(c, VV_LAYOUT(.dir = VV_ROW, .main = VV_ALIGN_CENTER, .w = vv_grow(1)),
                       VV_STYLE(.bg = {0})) {
                    vv_button(c, "again", "Play again", MSG_START, VV_NO_PAYLOAD);
                }
                break;
            }
            }
        }
    }
}

// ---- host loop ------------------------------------------------------------

int main(void) {
    srand((unsigned)time(NULL));

    vv_App *app = vv_app_create("Verve \xc2\xb7 Trivia", 720, 640);
    if (!app) return 1;
    const char *fonts[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                           "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/TTF/DejaVuSans.ttf", NULL};
    for (int i = 0; fonts[i]; i++) if (vv_app_load_font(app, fonts[i])) break;

    vv_Ctx ctx; vv_init(&ctx);
    vv_set_measure_fn(&ctx, vv_app_measure, app);
    // VV_SHOT=<png> captures a frame headlessly (dev/CI). While capturing we
    // keep rendering (no idle) and auto-start a round so the shot lands on a
    // real question instead of the idle menu.
    bool shot = getenv("VV_SHOT") != NULL;
    vv_set_idle_mode(&ctx, !shot);
    TH = vv_theme();

    vv_NetConfig cfg = vv_net_config_default();
    Game game = { .net = vv_net_create(&cfg), .ctx = &ctx, .picked = -1 };
    if (!game.net) { vv_app_destroy(app); return 1; }
    if (shot) vv_emit(&ctx, MSG_START, VV_NO_PAYLOAD);

    vv_Input in = {0};
    uint64_t prev = SDL_GetPerformanceCounter();
    while (vv_app_pump(app, &in)) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)SDL_GetPerformanceFrequency();
        prev = now;

        // Blessed integration: move network completions onto the UI queue BEFORE
        // vv_run_frame drains it, so a finished fetch becomes a normal message.
        vv_net_dispatch(game.net, &ctx);
        // While a request is in flight, keep rebuilding so the progress bar
        // animates and the completion is picked up promptly.
        if (game.pending) vv_invalidate(&ctx);

        int w, h; float dpi; vv_app_size(app, &w, &h, &dpi);
        vv_set_window(&ctx, (float)w, (float)h, dpi);

        vv_CommandBuffer *cmds = vv_run_frame(&ctx, dt, &in, update, view, &game);
        vv_app_set_cursor(app, vv_cursor(&ctx));
        if (cmds) {
            vv_app_frame_begin(app, TH->surface_app);
            vv_render(vv_app_backend(app), cmds, w, h, dpi);
            vv_app_frame_end(app);
        } else {
            vv_app_wait_event(app, 16);
        }
    }

    if (game.pending) vv_net_request_release(game.pending);
    vv_net_destroy(game.net);
    vv_shutdown(&ctx);
    vv_app_destroy(app);
    return 0;
}
