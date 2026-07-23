// vv_net.c — optional HTTP/JSON client built on libcurl + yyjson (§ networking).
//
// NOT part of the core library: this file compiles into libverve-net.a, linked
// only by applications that use networking. The core (libverve.a) never sees
// curl or yyjson.
//
// Threading model
// ---------------
//   * One vv_Async worker thread per request (no curl_multi in v1).
//   * The worker performs all curl I/O and JSON parsing, fills the (private)
//     response, then publishes it with a release store to `complete`.
//   * The UI thread observes completion with an acquire load, joins the worker
//     (vv_async_free), and consumes the result. Workers NEVER touch vv_Ctx or
//     call vv_emit.
//   * Releasing a running request is non-blocking: it cancels + abandons. The
//     service reaps the worker when the completion is later drained.
//
// Ownership
// ---------
//   A request is always on exactly one of the service's two lists (`inflight`
//   or the completed queue), reusing the single `next` link. `net_request_free`
//   assumes the worker has already been joined (r->async == NULL); every path
//   that frees a request joins first.
#define _DEFAULT_SOURCE  // strcasecmp
#include "verve/vv_net.h"
#include "verve/vv_async.h"
#include "verve/vv_event.h"

#include <curl/curl.h>
#include <yyjson.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===========================================================================
//  Opaque types
// ===========================================================================

struct vv_Net {
    vv_NetConfig    config;
    pthread_mutex_t lock;

    // Requests whose worker has not yet been reaped. Protected by `lock`.
    vv_NetRequest  *inflight;         // singly-linked (head), via r->next
    // Completed + reaped requests awaiting poll/dispatch, in completion order.
    vv_NetRequest  *completed_head;   // via r->next
    vv_NetRequest  *completed_tail;
    bool            shutting_down;
};

// vv_JsonValue is an opaque, borrowed alias for a yyjson_val living inside the
// document — stable for the document's lifetime. We never define its layout;
// callers only ever hold `const vv_JsonValue *`, which we reinterpret as a
// yyjson_val *. This keeps every accessor a pure function of its argument (no
// per-thread scratch, so two values can be held at once).
#define JVAL(v)  ((yyjson_val *)(v))
#define VVAL(y)  ((const vv_JsonValue *)(y))

struct vv_JsonDoc {
    yyjson_doc *doc;
};

struct vv_NetRequest {
    vv_Net       *net;
    vv_Async     *async;   // UI-thread handle: assigned by vv_net_start after
                           // the worker starts, so the worker must NOT read it
                           // (it uses its own `self` instead — see net_worker).

    // ---- copied spec inputs (owned; freed on release) ----
    char         *method;
    char         *url;
    vv_NetHeader *headers;            // array of {name,value} copies
    size_t        header_count;
    uint8_t      *body;
    size_t        body_len;

    uint32_t      connect_timeout_ms;
    uint32_t      timeout_ms;
    size_t        max_response_bytes;
    bool          follow_redirects;
    bool          parse_json;
    vv_Msg        complete_msg;
    void         *tag;

    // ---- cross-thread state ----
    atomic_bool   complete;          // release (worker) / acquire (service)
    _Atomic float progress;          // 0..1 transfer fraction

    // ---- worker-private until `complete` is published ----
    vv_NetResponse response;
    uint8_t      *body_buf;          // accumulation; handed to response.body
    size_t        body_cap;
    bool          size_exceeded;     // write callback hit max_response_bytes

    vv_NetHeader *resp_headers;
    size_t        resp_header_count;
    size_t        resp_header_cap;

    // ---- service bookkeeping ----
    vv_NetRequest *next;             // link for inflight OR completed list
    bool           abandoned;        // released while still running
};

// ===========================================================================
//  Config
// ===========================================================================

vv_NetConfig vv_net_config_default(void) {
    return (vv_NetConfig){
        .wake_fn            = NULL,
        .wake_ud            = NULL,
        .connect_timeout_ms = 30000,
        .timeout_ms         = 60000,
        .max_response_bytes = 1u << 20,  // 1 MiB
        .max_redirects      = 5,
    };
}

// ===========================================================================
//  Small helpers
// ===========================================================================

static char *net_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// Free a fully-owned request. The worker MUST already be joined (async == NULL).
static void net_request_free(vv_NetRequest *r) {
    if (!r) return;
    free(r->method);
    free(r->url);
    for (size_t i = 0; i < r->header_count; i++) {
        free((char *)r->headers[i].name);
        free((char *)r->headers[i].value);
    }
    free(r->headers);
    free(r->body);
    free(r->body_buf);
    for (size_t i = 0; i < r->resp_header_count; i++) {
        free((char *)r->resp_headers[i].name);
        free((char *)r->resp_headers[i].value);
    }
    free(r->resp_headers);
    if (r->response.json) {
        yyjson_doc_free(r->response.json->doc);
        free((vv_JsonDoc *)r->response.json);
    }
    free((char *)r->response.error_message);
    free(r);
}

// Join the worker if it is still attached. Safe to call more than once.
static void net_request_join(vv_NetRequest *r) {
    if (r->async) {
        vv_async_free(r->async);  // joins the thread
        r->async = NULL;
    }
}

// Detach `r` from a singly-linked list rooted at *head (tail optional).
static void net_list_unlink(vv_NetRequest **head, vv_NetRequest **tail,
                            vv_NetRequest *r) {
    vv_NetRequest *prev = NULL, *cur = *head;
    while (cur) {
        if (cur == r) {
            if (prev) prev->next = cur->next;
            else      *head = cur->next;
            if (tail && *tail == cur) *tail = prev;
            r->next = NULL;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

// Append to the completed queue (tail).
static void net_completed_push(vv_Net *net, vv_NetRequest *r) {
    r->next = NULL;
    if (net->completed_tail) net->completed_tail->next = r;
    else                     net->completed_head = r;
    net->completed_tail = r;
}

// Move every finished inflight request onto the completed queue, joining its
// worker. Must hold net->lock.
static void net_reap_inflight(vv_Net *net) {
    vv_NetRequest *prev = NULL, *r = net->inflight;
    while (r) {
        vv_NetRequest *next = r->next;
        if (atomic_load_explicit(&r->complete, memory_order_acquire)) {
            if (prev) prev->next = next;
            else      net->inflight = next;
            net_request_join(r);
            net_completed_push(net, r);
        } else {
            prev = r;
        }
        r = next;
    }
}

// ===========================================================================
//  Service lifecycle
// ===========================================================================

vv_Net *vv_net_create(const vv_NetConfig *config) {
    vv_Net *net = calloc(1, sizeof *net);
    if (!net) return NULL;
    net->config = config ? *config : vv_net_config_default();
    if (pthread_mutex_init(&net->lock, NULL) != 0) { free(net); return NULL; }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return net;
}

void vv_net_destroy(vv_Net *net) {
    if (!net) return;

    pthread_mutex_lock(&net->lock);
    net->shutting_down = true;

    // Cancel + join every in-flight worker, then free.
    vv_NetRequest *r = net->inflight;
    while (r) {
        vv_NetRequest *next = r->next;
        vv_async_cancel(r->async);
        net_request_join(r);
        net_request_free(r);
        r = next;
    }
    net->inflight = NULL;

    // Free any completed-but-undrained requests (already joined).
    r = net->completed_head;
    while (r) {
        vv_NetRequest *next = r->next;
        net_request_free(r);
        r = next;
    }
    net->completed_head = net->completed_tail = NULL;
    pthread_mutex_unlock(&net->lock);

    pthread_mutex_destroy(&net->lock);
    curl_global_cleanup();
    free(net);
}

// ===========================================================================
//  Curl callbacks (all run on the worker thread)
// ===========================================================================

// Accumulate body bytes, bounded by max_response_bytes.
static size_t net_write_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    vv_NetRequest *r = ud;
    size_t total = size * nmemb;
    size_t have  = r->response.body_len;

    if (have + total > r->max_response_bytes) {
        r->size_exceeded = true;
        return 0;  // aborts the transfer (CURLE_WRITE_ERROR)
    }
    if (have + total > r->body_cap) {
        size_t cap = r->body_cap ? r->body_cap : 65536;
        while (cap < have + total) cap *= 2;
        uint8_t *nb = realloc(r->body_buf, cap);
        if (!nb) return 0;  // OOM → treated as transport error
        r->body_buf = nb;
        r->body_cap = cap;
    }
    memcpy(r->body_buf + have, ptr, total);
    r->response.body_len = have + total;
    return total;
}

// Parse one response header line ("Name: value\r\n") into the header array.
static size_t net_header_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    vv_NetRequest *r = ud;
    size_t total = size * nmemb;

    // Skip the status line and the blank separator line.
    if (total < 2 || strncmp(ptr, "HTTP/", 5) == 0 ||
        ptr[0] == '\r' || ptr[0] == '\n')
        return total;

    char *colon = memchr(ptr, ':', total);
    if (!colon) return total;

    size_t name_len = (size_t)(colon - ptr);
    size_t vs = name_len + 1;
    while (vs < total && (ptr[vs] == ' ' || ptr[vs] == '\t')) vs++;
    size_t val_len = total - vs;
    while (val_len > 0 && (ptr[vs + val_len - 1] == '\r' ||
                           ptr[vs + val_len - 1] == '\n'))
        val_len--;

    if (r->resp_header_count >= r->resp_header_cap) {
        size_t cap = r->resp_header_cap ? r->resp_header_cap * 2 : 16;
        vv_NetHeader *nh = realloc(r->resp_headers, cap * sizeof *nh);
        if (!nh) return 0;
        r->resp_headers = nh;
        r->resp_header_cap = cap;
    }

    char *name = malloc(name_len + 1);
    char *val  = malloc(val_len + 1);
    if (!name || !val) { free(name); free(val); return 0; }
    memcpy(name, ptr, name_len);      name[name_len] = '\0';
    memcpy(val, ptr + vs, val_len);   val[val_len]   = '\0';
    r->resp_headers[r->resp_header_count++] = (vv_NetHeader){ name, val };
    return total;
}

// Progress-callback context: the request plus the worker's own async handle.
// Lives on net_worker's stack for the duration of curl_easy_perform, which is
// where curl invokes this callback — so `self` is used instead of r->async
// (which the worker must not touch; see net_worker).
typedef struct { vv_NetRequest *r; vv_Async *self; } net_xfer_ctx;

// Report progress and honour cooperative cancellation. Returning non-zero
// aborts the transfer (CURLE_ABORTED_BY_CALLBACK).
static int net_xfer_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    net_xfer_ctx *x = ud;
    (void)ultotal; (void)ulnow;
    if (vv_async_cancelled(x->self)) return 1;
    if (dltotal > 0)
        atomic_store_explicit(&x->r->progress,
                              (float)((double)dlnow / (double)dltotal),
                              memory_order_relaxed);
    return 0;
}

// ===========================================================================
//  Worker
// ===========================================================================

// Publish the finished response and wake an idle host. Called once, last.
static void net_publish(vv_NetRequest *r) {
    r->response.headers      = r->resp_headers;
    r->response.header_count = r->resp_header_count;
    r->response.body         = r->body_buf;  // body_len already tracked
    atomic_store_explicit(&r->complete, true, memory_order_release);
    if (r->net->config.wake_fn)
        r->net->config.wake_fn(r->net->config.wake_ud);
}

static void net_fail(vv_NetRequest *r, vv_NetError err, const char *msg) {
    r->response.error         = err;
    r->response.error_message = net_strdup(msg);
    r->response.status        = 0;
}

static void *net_worker(vv_Async *self, void *arg) {
    vv_NetRequest *r = arg;
    net_xfer_ctx xfer = { .r = r, .self = self };

    CURL *curl = curl_easy_init();
    if (!curl) { net_fail(r, VV_NET_ERR_TRANSPORT, "curl_easy_init failed");
                 net_publish(r); return NULL; }

    curl_easy_setopt(curl, CURLOPT_URL, r->url);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);            // thread-safe
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");     // allow gzip/deflate

    // Method + body. GET is curl's default; everything else is set explicitly.
    // A body is attached for any method that carries one.
    bool is_get = strcmp(r->method, "GET") == 0;
    if (!is_get)
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, r->method);
    if (!is_get && r->body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, r->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)r->body_len);
    }

    struct curl_slist *slist = NULL;
    for (size_t i = 0; i < r->header_count; i++) {
        // "Name: value" — libcurl copies the string.
        size_t n = strlen(r->headers[i].name) + strlen(r->headers[i].value) + 3;
        char *line = malloc(n);
        if (line) {
            snprintf(line, n, "%s: %s", r->headers[i].name, r->headers[i].value);
            slist = curl_slist_append(slist, line);
            free(line);
        }
    }
    if (slist) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)r->connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)r->timeout_ms);

    if (r->follow_redirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, r->net->config.max_redirects);
    }

    // TLS verification stays on (curl's default). Plain-http fixtures are
    // unaffected; real HTTPS endpoints are verified.
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, net_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, net_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, r);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, net_xfer_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &xfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        r->response.error  = VV_NET_OK;
        r->response.status = status;
        atomic_store_explicit(&r->progress, 1.0f, memory_order_relaxed);

        if (r->parse_json && r->body_buf && r->response.body_len > 0) {
            yyjson_read_err err;
            yyjson_doc *doc = yyjson_read_opts((char *)r->body_buf,
                                               r->response.body_len,
                                               YYJSON_READ_NOFLAG, NULL, &err);
            if (doc) {
                vv_JsonDoc *jd = calloc(1, sizeof *jd);
                if (jd) { jd->doc = doc; r->response.json = jd; }
                else    { yyjson_doc_free(doc);
                          net_fail(r, VV_NET_ERR_JSON, "out of memory parsing JSON"); }
            } else {
                char buf[160];
                snprintf(buf, sizeof buf, "JSON parse error: %s (at byte %zu)",
                         err.msg ? err.msg : "unknown", err.pos);
                r->response.error = VV_NET_ERR_JSON;
                r->response.error_message = net_strdup(buf);
            }
        }
    } else if (r->size_exceeded) {
        net_fail(r, VV_NET_ERR_RESPONSE_TOO_LARGE,
                 "response exceeded max_response_bytes");
    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        net_fail(r, VV_NET_ERR_TIMEOUT, curl_easy_strerror(res));
    } else if (res == CURLE_ABORTED_BY_CALLBACK) {
        net_fail(r, VV_NET_ERR_CANCELLED, "cancelled");
    } else {
        net_fail(r, VV_NET_ERR_TRANSPORT, curl_easy_strerror(res));
    }
    if (res != CURLE_OK) r->response.curl_code = (int)res;

    if (slist) curl_slist_free_all(slist);
    curl_easy_cleanup(curl);

    net_publish(r);
    return NULL;
}

// ===========================================================================
//  Request creation
// ===========================================================================

vv_NetRequest *vv_net_start(vv_Net *net, const vv_NetSpec *spec) {
    if (!net || !spec || !spec->url) return NULL;

    vv_NetRequest *r = calloc(1, sizeof *r);
    if (!r) return NULL;

    r->net                = net;
    r->method             = net_strdup(spec->method ? spec->method : "GET");
    r->url                = net_strdup(spec->url);
    r->body_len           = spec->body_len;
    r->complete_msg       = spec->complete_msg;
    r->tag                = spec->tag;
    r->parse_json         = spec->parse_json;
    r->follow_redirects   = spec->follow_redirects;
    r->connect_timeout_ms = spec->connect_timeout_ms ? spec->connect_timeout_ms
                                                      : net->config.connect_timeout_ms;
    r->timeout_ms         = spec->timeout_ms ? spec->timeout_ms
                                             : net->config.timeout_ms;
    r->max_response_bytes = spec->max_response_bytes ? spec->max_response_bytes
                                                     : net->config.max_response_bytes;
    if (!r->method || !r->url) { net_request_free(r); return NULL; }

    if (spec->body && spec->body_len > 0) {
        r->body = malloc(spec->body_len);
        if (!r->body) { net_request_free(r); return NULL; }
        memcpy(r->body, spec->body, spec->body_len);
    }

    if (spec->headers && spec->header_count > 0) {
        r->header_count = spec->header_count;
        r->headers = calloc(spec->header_count, sizeof *r->headers);
        if (!r->headers) { net_request_free(r); return NULL; }
        for (size_t i = 0; i < spec->header_count; i++) {
            r->headers[i].name  = net_strdup(spec->headers[i].name);
            r->headers[i].value = net_strdup(spec->headers[i].value);
        }
    }

    atomic_store(&r->complete, false);
    atomic_store(&r->progress, 0.0f);

    r->async = vv_async_run(net_worker, r);
    if (!r->async) { net_request_free(r); return NULL; }

    pthread_mutex_lock(&net->lock);
    r->next = net->inflight;
    net->inflight = r;
    pthread_mutex_unlock(&net->lock);
    return r;
}

// ===========================================================================
//  Response accessors
// ===========================================================================

const vv_NetResponse *vv_net_response(vv_NetRequest *r) {
    if (!r || !atomic_load_explicit(&r->complete, memory_order_acquire))
        return NULL;
    return &r->response;
}

bool vv_net_succeeded(vv_NetRequest *r) {
    const vv_NetResponse *resp = vv_net_response(r);
    return resp && resp->error == VV_NET_OK &&
           resp->status >= 200 && resp->status < 300;
}

vv_NetError vv_net_error(vv_NetRequest *r) {
    const vv_NetResponse *resp = vv_net_response(r);
    return resp ? resp->error : VV_NET_OK;
}

float vv_net_progress(vv_NetRequest *r) {
    return r ? atomic_load_explicit(&r->progress, memory_order_relaxed) : 0.0f;
}

const char *vv_net_header(const vv_NetRequest *r, const char *name) {
    if (!r || !name) return NULL;
    if (!atomic_load_explicit(&r->complete, memory_order_acquire)) return NULL;
    for (size_t i = 0; i < r->resp_header_count; i++)
        if (strcasecmp(r->resp_headers[i].name, name) == 0)
            return r->resp_headers[i].value;
    return NULL;
}

// ===========================================================================
//  Release
// ===========================================================================

void vv_net_request_release(vv_NetRequest *r) {
    if (!r) return;
    vv_Net *net = r->net;

    pthread_mutex_lock(&net->lock);
    if (atomic_load_explicit(&r->complete, memory_order_acquire)) {
        // Worker done: unlink from whichever list holds it, join, free.
        net_list_unlink(&net->inflight, NULL, r);
        net_list_unlink(&net->completed_head, &net->completed_tail, r);
        net_request_join(r);
        pthread_mutex_unlock(&net->lock);
        net_request_free(r);
        return;
    }
    // Still running: cancel + abandon (non-blocking). Reaped later.
    vv_async_cancel(r->async);
    r->abandoned = true;
    pthread_mutex_unlock(&net->lock);
}

// ===========================================================================
//  Polling / dispatch
// ===========================================================================

bool vv_net_poll(vv_Net *net, vv_NetCompletion *out) {
    if (!net) return false;
    pthread_mutex_lock(&net->lock);
    net_reap_inflight(net);

    // Skip past (and free) abandoned completions; return the first live one.
    while (net->completed_head) {
        vv_NetRequest *r = net->completed_head;
        net->completed_head = r->next;
        if (!net->completed_head) net->completed_tail = NULL;

        if (r->abandoned) { net_request_free(r); continue; }

        if (out) *out = (vv_NetCompletion){ .request = r, .msg = r->complete_msg,
                                            .tag = r->tag };
        pthread_mutex_unlock(&net->lock);
        return true;
    }
    pthread_mutex_unlock(&net->lock);
    return false;
}

// Forward declaration into core (dependency-neutral; no vv_Ctx layout needed).
struct vv_Ctx;
void vv_emit(struct vv_Ctx *ctx, vv_Msg msg, vv_Payload data);

size_t vv_net_dispatch(vv_Net *net, struct vv_Ctx *ctx) {
    if (!net || !ctx) return 0;
    pthread_mutex_lock(&net->lock);
    net_reap_inflight(net);

    size_t emitted = 0;
    vv_NetRequest *r = net->completed_head;
    while (r) {
        vv_NetRequest *next = r->next;
        if (r->abandoned) {
            // Owner is gone: consume + free.
            net_list_unlink(&net->completed_head, &net->completed_tail, r);
            net_request_free(r);
        } else if (r->complete_msg != VV_MSG_NONE) {
            // Deliver as a normal UI message; the app releases the request.
            // VV_MSG_NONE requests are left in place for vv_net_poll.
            net_list_unlink(&net->completed_head, &net->completed_tail, r);
            vv_emit(ctx, r->complete_msg, vv_pp(r));
            emitted++;
        }
        r = next;
    }
    pthread_mutex_unlock(&net->lock);
    return emitted;
}

// ===========================================================================
//  JSON adapter (thin read-only view over yyjson)
// ===========================================================================

const vv_JsonValue *vv_json_root(const vv_JsonDoc *doc) {
    return doc ? VVAL(yyjson_doc_get_root(doc->doc)) : NULL;
}

vv_JsonKind vv_json_kind(const vv_JsonValue *value) {
    if (!value) return VV_JSON_NULL;
    switch (yyjson_get_type(JVAL(value))) {
    case YYJSON_TYPE_BOOL: return VV_JSON_BOOL;
    case YYJSON_TYPE_NUM:  return VV_JSON_NUMBER;
    case YYJSON_TYPE_STR:  return VV_JSON_STRING;
    case YYJSON_TYPE_ARR:  return VV_JSON_ARRAY;
    case YYJSON_TYPE_OBJ:  return VV_JSON_OBJECT;
    default:               return VV_JSON_NULL;
    }
}

const vv_JsonValue *vv_json_object_get(const vv_JsonValue *obj, const char *key) {
    if (!obj || !key) return NULL;
    return VVAL(yyjson_obj_get(JVAL(obj), key));
}

size_t vv_json_array_count(const vv_JsonValue *arr) {
    return arr ? yyjson_arr_size(JVAL(arr)) : 0;
}

const vv_JsonValue *vv_json_array_at(const vv_JsonValue *arr, size_t index) {
    if (!arr) return NULL;
    return VVAL(yyjson_arr_get(JVAL(arr), index));
}

bool vv_json_string(const vv_JsonValue *val, const char **ptr, size_t *len) {
    if (!val || !yyjson_is_str(JVAL(val))) return false;
    if (ptr) *ptr = yyjson_get_str(JVAL(val));
    if (len) *len = yyjson_get_len(JVAL(val));
    return true;
}

bool vv_json_number(const vv_JsonValue *val, double *out) {
    if (!val || !yyjson_is_num(JVAL(val))) return false;
    if (out) *out = yyjson_get_num(JVAL(val));
    return true;
}

bool vv_json_bool(const vv_JsonValue *val, bool *out) {
    if (!val || !yyjson_is_bool(JVAL(val))) return false;
    if (out) *out = yyjson_get_bool(JVAL(val));
    return true;
}
