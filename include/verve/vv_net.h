// vv_net.h — optional HTTP client for Verve (§ networking).
//
// A small, dependency-neutral HTTP request/response layer built on libcurl and
// yyjson. It is NOT part of the core library: applications that don't use it
// never link libcurl. Include verve.h to get this header; link libverve-net.a
// (+ libcurl + yyjson) only when you call these functions.
//
// Design summary (see networking.md for the full rationale):
//   * One vv_async worker per request (no curl_multi in v1).
//   * Request inputs are copied at vv_net_start — stack buffers are safe.
//   * Responses are immutable, heap-backed, and binary-safe.
//   * Workers never touch vv_Ctx; completion is delivered through the message
//     queue on the UI thread via vv_net_dispatch or vv_net_poll.
//   * Releasing a running request is non-blocking (cancel + abandon).
//   * JSON is parsed off the UI thread behind an opaque wrapper.
#ifndef VV_NET_H
#define VV_NET_H

#include "vv_event.h"  // vv_Msg
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declaration for vv_net_dispatch.
struct vv_Ctx;

// ---- opaque handles -------------------------------------------------------

typedef struct vv_Net vv_Net;
typedef struct vv_NetRequest vv_NetRequest;
typedef struct vv_JsonDoc vv_JsonDoc;
typedef struct vv_JsonValue vv_JsonValue;

// ---- configuration --------------------------------------------------------

// Optional host-loop wake hook. Called (on a worker thread) when a request
// completes, so an idle host can break out of its event wait. The hook MUST
// NOT touch Verve state or call user code — it may only signal an event loop
// (e.g. write to a pipe, post a Win32 message, set a flag). If NULL, hosts
// should use a bounded wait (see vv_app_wait_event).
typedef void (*vv_NetWakeFn)(void *ud);

typedef struct {
    vv_NetWakeFn wake_fn;
    void        *wake_ud;
    uint32_t     connect_timeout_ms;  // default: 30000
    uint32_t     timeout_ms;          // default: 60000
    size_t       max_response_bytes;  // default: 1 MiB
    long         max_redirects;       // default: 5 (only when follow_redirects)
} vv_NetConfig;

// Sensible defaults for vv_NetConfig.
vv_NetConfig vv_net_config_default(void);

// Create/destroy the network service. Create once per application (or per
// independent network policy). Destroy after all contexts stop using it; this
// cancels in-flight requests, joins workers, and calls curl_global_cleanup.
vv_Net *vv_net_create(const vv_NetConfig *config);
void    vv_net_destroy(vv_Net *net);

// ---- request specification ------------------------------------------------

typedef struct {
    const char *name;
    const char *value;
} vv_NetHeader;

typedef struct {
    const char *method;            // "GET", "POST", "PUT", "PATCH", "DELETE"
    const char *url;
    const vv_NetHeader *headers;
    size_t header_count;
    const void *body;              // copied at start; may be NULL
    size_t body_len;

    // Per-request overrides. Zero means "use the service default".
    uint32_t connect_timeout_ms;
    uint32_t timeout_ms;
    size_t max_response_bytes;
    bool follow_redirects;
    bool parse_json;

    vv_Msg complete_msg;  // VV_MSG_NONE => poll manually with vv_net_poll
    void *tag;            // application identity, never dereferenced by Verve
} vv_NetSpec;

// Start a request. Copies all spec inputs (strings, headers, body). Returns a
// stable handle valid until vv_net_request_release. Never blocks.
vv_NetRequest *vv_net_start(vv_Net *net, const vv_NetSpec *spec);

// ---- response -------------------------------------------------------------

typedef enum {
    VV_NET_OK = 0,
    VV_NET_ERR_CANCELLED,
    VV_NET_ERR_TIMEOUT,
    VV_NET_ERR_TRANSPORT,
    VV_NET_ERR_RESPONSE_TOO_LARGE,
    VV_NET_ERR_JSON,
} vv_NetError;

typedef struct {
    vv_NetError error;
    int         curl_code;       // diagnostic, not required for app logic
    long        status;          // 0 when no HTTP response was received
    const char *error_message;   // request-owned, stable until release

    const vv_NetHeader *headers;
    size_t header_count;
    const uint8_t *body;         // binary-safe; may contain NUL bytes
    size_t body_len;

    const vv_JsonDoc *json;      // non-NULL only when parse_json succeeded
} vv_NetResponse;

// Get the immutable response. Safe to call after the request is complete (and
// before release). Returns NULL if the request hasn't completed yet.
const vv_NetResponse *vv_net_response(vv_NetRequest *r);

// Status helpers. A 2xx check is a policy choice — use vv_net_succeeded for
// "no transport error and status in [200,300)".
bool vv_net_succeeded(vv_NetRequest *r);
vv_NetError vv_net_error(vv_NetRequest *r);

// Atomic snapshot of transfer progress (0..1) for an optional progress bar.
float vv_net_progress(vv_NetRequest *r);

// Case-insensitive header lookup. Returns a borrowed pointer valid until
// release, or NULL if not found.
const char *vv_net_header(const vv_NetRequest *r, const char *name);

// Non-blocking release. If the request is complete, frees everything
// immediately. If still running, cancels and marks abandoned — the worker is
// joined by the service when it finishes (during poll/dispatch/destroy).
void vv_net_request_release(vv_NetRequest *r);

// ---- completion delivery --------------------------------------------------

typedef struct {
    vv_NetRequest *request;
    vv_Msg         msg;
    void          *tag;
} vv_NetCompletion;

// Low-level polling for custom loops. Returns true and fills `out` if a
// completion is available. Does not touch vv_Ctx. A request with
// complete_msg == VV_MSG_NONE is still enqueued here.
bool vv_net_poll(vv_Net *net, vv_NetCompletion *out);

// Verve adapter: drain all completions and emit each request's complete_msg
// (with vv_pp(request)) into the context's message queue. Must be called on the
// UI thread, before vv_run_frame. Returns the number of messages emitted.
// Completions for abandoned requests are consumed silently (worker joined,
// request freed).
size_t vv_net_dispatch(vv_Net *net, struct vv_Ctx *ctx);

// ---- JSON adapter (thin read-only wrapper over yyjson) -------------------

typedef enum {
    VV_JSON_NULL,
    VV_JSON_BOOL,
    VV_JSON_NUMBER,
    VV_JSON_STRING,
    VV_JSON_ARRAY,
    VV_JSON_OBJECT,
} vv_JsonKind;

const vv_JsonValue *vv_json_root(const vv_JsonDoc *doc);
vv_JsonKind         vv_json_kind(const vv_JsonValue *value);
const vv_JsonValue *vv_json_object_get(const vv_JsonValue *obj, const char *key);
size_t              vv_json_array_count(const vv_JsonValue *arr);
const vv_JsonValue *vv_json_array_at(const vv_JsonValue *arr, size_t index);
bool vv_json_string(const vv_JsonValue *val, const char **ptr, size_t *len);
bool vv_json_number(const vv_JsonValue *val, double *out);
bool vv_json_bool(const vv_JsonValue *val, bool *out);

#endif // VV_NET_H
