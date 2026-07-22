# Networking plan for Verve

## 1. Summary and recommendation

Add an optional, small HTTP client module rather than making networking part of the
rendering/core pipeline.

The recommended first implementation is:

- **libcurl** for HTTP, TLS, redirects, proxies, compression, DNS, status codes,
  cancellation, and response transfer. Verve must not implement sockets, HTTP
  parsing, TLS, or chunked transfer handling.
- **yyjson** (or another similarly small, permissively licensed established
  library selected during Phase 0) for JSON decoding. Verve must not implement a
  JSON tokenizer, parser, or serializer.
- The existing **`vv_async`** worker abstraction for the first transport
  implementation: one request is one background job. This keeps the initial
  stack understandable and reuses cooperative cancellation and progress.
- A **main-thread completion queue** and a small adapter into Verve's existing
  message/update/view loop. Worker threads never call `vv_emit`, `vv_invalidate`,
  `view`, or user callbacks.
- An optional **`libverve-net.a`** so applications that do not use networking do
  not acquire a libcurl dependency. The existing `libverve.a` and its backend-free
  nature remain intact.

This is intentionally an HTTP request/response layer, not a general-purpose
network framework. It should be pleasant for a UI application to load JSON data,
submit a form, refresh a resource, or download a bounded blob without forcing
that application to learn another event system.

## 2. Current architecture constraints

The plan should preserve the following properties already present in the codebase:

1. **Message/update/view is the application model.** Widgets emit messages,
   `vv_run_frame` drains them into `update`, and `view` declares the UI. Network
   completion should look like another message, not a callback that mutates app
   state on an arbitrary thread.
2. **`vv_async` is poll-oriented.** It starts a pthread, supports cooperative
   cancellation and progress, and reports completion through polling. It does
   not currently wake an idle host loop or know about `vv_Ctx`.
3. **The frame arena is transient.** A network response can live for many frames,
   so response bytes must not be stored in `ctx->frame`, `ctx->present`, or a
   `vv_Str` whose storage is reset on a frame boundary.
4. **`vv_Ctx` is not thread-safe.** The worker must only write to request-local
   memory and atomics. All interaction with the context and event queue happens
   on the UI thread.
5. **Idle mode matters.** A completed request must invalidate or enqueue work in
   a way that causes the next `vv_run_frame` to build. A request that completes
   while the host is sleeping must also have a bounded polling/wakeup strategy.
6. **The library is C11 and already uses pthreads.** The public API should not
   require C++ types, callbacks with hidden lifetime rules, or third-party types
   such as `CURL *` or `yyjson_val *` in public structs.

The implementation should also acknowledge that network responses are one of the
places where the existing “arenas only” memory ideal is not appropriate. A
response has an application-controlled lifetime and crosses a thread boundary;
it should use an explicit heap-backed, immutable buffer with a clear release
operation rather than being forced into a frame arena.

## 3. Explicit goals

### 3.1 Functional goals

- Start an HTTP request from `update` without blocking the UI thread.
- Support the common methods: `GET`, `POST`, `PUT`, `PATCH`, and `DELETE`.
- Send arbitrary byte bodies and caller-supplied headers.
- Receive status, headers, an owned binary-safe body, transfer metadata, and a
  classified error.
- Enforce connect, total, and response-size limits.
- Support cooperative cancellation.
- Make HTTP status failures distinct from transport failures. A `404` is a valid
  HTTP response with an unsuccessful status, not the same thing as a DNS failure.
- Optionally parse a response as JSON on the worker thread, without blocking the
  UI thread.
- Deliver completion through the existing message queue on the UI thread.
- Keep results valid until the application explicitly releases them.
- Make request behavior deterministic enough to test with a local fixture server.

### 3.2 Integration goals

The normal flow should be recognizable as Verve code:

```c
// update(): state changes and starts work.
a->request = vv_net_start(a->net, &(vv_NetSpec){
    .method = "GET",
    .url = "https://example.test/items",
    .parse_json = true,
    .complete_msg = MSG_ITEMS_COMPLETE,
});

// host loop, before vv_run_frame(): completion is moved to the UI queue.
vv_net_dispatch(a->net, &ctx);

// update(): consume the immutable result.
case MSG_ITEMS_COMPLETE: {
    vv_NetRequest *r = ev.data.as_ptr;
    const vv_NetResponse *response = vv_net_response(r);
    // Copy only the application data needed by state.
    // Do not mutate the view from here or from the worker.
    ...
    vv_net_request_release(r);
    break;
}
```

The exact names may change, but the important shape is fixed: start in `update`,
render pending state in `view`, dispatch completion on the UI thread, handle the
message in `update`, and invalidate naturally through `vv_run_frame`.

### 3.3 Non-goals for v1

Do not include these in the first stack:

- A hand-written HTTP client, socket layer, TLS layer, or JSON parser.
- WebSockets, WebRTC, an HTTP server, or arbitrary UDP/TCP APIs.
- Streaming response callbacks into user code.
- Automatic retries, caching, cookie jars, OAuth, multipart upload, or a
  generated API/client schema layer.
- A global observable/store system separate from `vv_Event`.
- JSON mutation, JSON serialization, JSONPath, reflection, or model generation.
- Automatic polling/revalidation of every request.
- Automatic UI widgets for loading/error states. Those should remain ordinary
  user-authored Verve views.

Retries and caching can be added later as policies around the request layer. They
should not be hidden defaults, especially because retrying a non-idempotent POST
is surprising.

## 4. Proposed public surface

The API should be opaque and dependency-neutral. No public declaration should
mention libcurl or yyjson.

### 4.1 Request and service handles

Proposed opaque types:

```c
typedef struct vv_Net vv_Net;
typedef struct vv_NetRequest vv_NetRequest;
typedef struct vv_JsonDoc vv_JsonDoc;
typedef struct vv_JsonValue vv_JsonValue;
```

`vv_Net` owns configuration, in-flight request bookkeeping, and the completion
queue. It is created once per application (or once per independent network
policy/domain) and destroyed after all contexts stop using it.

`vv_NetRequest` is the stable application handle. It remains valid while the
request is running and while its completion/result is being consumed. Releasing a
running request must be non-blocking: mark it abandoned/cancelled and let the
network owner clean it up when the worker finishes. `vv_net_destroy` may join
remaining workers during application shutdown.

### 4.2 Request specification

Use a value struct whose strings and body are copied when `vv_net_start` is
called:

```c
typedef struct {
    const char *name;
    const char *value;
} vv_NetHeader;

typedef struct {
    const char *method;
    const char *url;
    const vv_NetHeader *headers;
    size_t header_count;
    const void *body;
    size_t body_len;

    uint32_t connect_timeout_ms;
    uint32_t timeout_ms;
    size_t max_response_bytes;
    bool follow_redirects;
    bool parse_json;

    vv_Msg complete_msg;  // VV_MSG_NONE means use vv_net_poll manually.
    void *tag;            // application identity, never dereferenced by Verve.
} vv_NetSpec;
```

Defaults should be safe and useful:

- TLS peer and host verification enabled.
- A finite connect timeout and total timeout.
- A finite response-size limit.
- No redirects unless enabled explicitly, with a bounded redirect count when
  enabled.
- No credentials or request headers logged by the library.
- No implicit retry.
- Body data copied before returning from `vv_net_start`, so stack buffers and
  temporary `vv_Str` values are safe.

Convenience constructors for common GET and JSON-accepting requests may be added
only after the struct API is working. Avoid a fluent builder API in C unless
repeated call sites prove that it materially improves readability.

### 4.3 Response and error model

The response should be immutable and heap-backed:

```c
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
    const uint8_t *body;
    size_t body_len;

    const vv_JsonDoc *json;      // non-NULL only when parse_json succeeded
} vv_NetResponse;
```

`body_len` is authoritative and the body must support embedded NUL bytes. It is
reasonable to append a NUL sentinel for convenient text use, but it must never
be required. Header lookup should be case-insensitive and should return borrowed
pointers valid for the request lifetime.

The API should expose both `vv_net_response(r)` and small status helpers such as
`vv_net_succeeded(r)` / `vv_net_error(r)`. A 2xx check must remain a policy
choice that is visible in the application or in a clearly named helper.

### 4.4 Completion delivery

Provide two paths:

1. **Low-level polling** for custom loops:

   ```c
   typedef struct {
       vv_NetRequest *request;
       vv_Msg msg;
       void *tag;
   } vv_NetCompletion;

   bool vv_net_poll(vv_Net *net, vv_NetCompletion *out);
   ```

2. **Verve adapter** for the blessed loop:

   ```c
   size_t vv_net_dispatch(vv_Net *net, vv_Ctx *ctx);
   ```

   `vv_net_dispatch` drains completions on the calling thread and emits each
   request's `complete_msg` with `vv_pp(request)`. It must never run on a worker
   thread. Because `vv_run_frame` drains messages before deciding whether to
   build, a dispatched completion automatically becomes a normal state update.

A request with `complete_msg == VV_MSG_NONE` stays available through
`vv_net_poll` and does not inject anything into a context. This keeps the network
module useful outside a UI loop.

`vv_net_progress` should be an atomic snapshot for an optional progress bar. Do
not emit one UI message for every transfer callback. Applications that want a
continuously updating progress display can call `vv_animate(ctx)` or explicitly
invalidate while a request is pending; completion remains event-driven.

To address idle hosts, add an optional wake hook to `vv_NetConfig`:

```c
typedef void (*vv_NetWakeFn)(void *ud);
```

The hook may signal an event loop, but it must not touch Verve state or call user
code. If no wake hook is installed, the host should use a bounded wait (the
existing async example already uses a short `vv_app_wait_event` timeout).

### 4.5 JSON adapter

Use a thin wrapper over the selected external parser. The wrapper should provide
stable, small operations such as:

```c
const vv_JsonValue *vv_json_root(const vv_JsonDoc *doc);
vv_JsonKind vv_json_kind(const vv_JsonValue *value);
const vv_JsonValue *vv_json_object_get(const vv_JsonValue *, const char *key);
size_t vv_json_array_count(const vv_JsonValue *);
const vv_JsonValue *vv_json_array_at(const vv_JsonValue *, size_t index);
bool vv_json_string(const vv_JsonValue *, const char **ptr, size_t *len);
bool vv_json_number(const vv_JsonValue *, double *out);
bool vv_json_bool(const vv_JsonValue *, bool *out);
```

The first version should be a read-only view, not a second DOM implementation.
The parsed document is owned by the response/request and is valid for the same
lifetime. JSON parsing happens in the network worker when `.parse_json` is set;
malformed JSON is reported as `VV_NET_ERR_JSON` with a useful offset/message.

Choose yyjson (or the equivalent dependency selected during Phase 0) because a
read-only document and high-throughput parser fit network responses well. Vendor
a pinned version if that is the project's preferred dependency model, otherwise
use a system package. Record its license and version in the build documentation.
Do not expose its headers in Verve's public API.

## 5. Internal architecture

### 5.1 Request lifecycle

1. `vv_net_start` validates the URL/method/options and copies request data.
2. The service creates a request record and starts a `vv_async` worker.
3. The worker initializes/configures a libcurl easy handle, installs bounded
   write/header/progress callbacks, performs the transfer, and optionally parses
   JSON into the request-owned result.
4. The worker publishes a complete immutable result using release/acquire
   synchronization and marks the request complete.
5. The worker enqueues a completion record. It does not access `vv_Ctx`.
6. The UI thread calls `vv_net_dispatch` or `vv_net_poll`.
7. The application consumes/copies what it needs and calls
   `vv_net_request_release`.

The write callback must stop accepting bytes as soon as the configured maximum is
reached. The progress callback should check the atomic cancellation flag so a
cancelled transfer exits libcurl promptly. All libcurl handles are created and
used by the same worker thread.

The first implementation intentionally does not pool connections or use
`curl_multi`. One `vv_async` job per request is simple, easy to debug, and matches
the current async API. If profiling shows thread creation or connection setup is
material for chat/search workloads, a later transport can replace the worker
implementation without changing the public request/result API.

### 5.2 Ownership and synchronization rules

- `vv_Net`, request handles, headers, body buffers, and response buffers have
  explicit release points.
- Request specification inputs are copied at start.
- Response data is immutable after publication.
- Atomic state/cancel/progress fields are the only cross-thread mutable fields.
- Completion queue operations use a mutex/condition variable or an equivalent
  bounded queue; do not assume the existing UI event ring is thread-safe.
- A request can be released by the UI while running, but its memory is retained
  by the network manager until the worker has stopped.
- Shutdown cancels all requests, joins workers, drains/frees results, then
  calls the libcurl global cleanup exactly once after the last service is gone.
- Never put a response pointer into the frame arena and never retain a pointer to
  user-owned request input.

### 5.3 Dependency and build isolation

Refactor the Makefile source lists rather than allowing `src/vv_net.c` to become
an accidental member of the core archive:

- `CORE_SRC`: current sources, including `vv_async.c`.
- `NET_SRC`: `vv_net.c` and the JSON adapter implementation.
- `libverve.a`: unchanged dependency surface.
- `libverve-net.a`: network objects, linked by applications together with
  `libverve.a`, libcurl, and the JSON implementation.

Add an explicit `net`/`lib` target and a documented command such as
`pkg-config --libs libcurl` rather than silently assuming curl is installed.
`verve.h` may include `vv_net.h` because the header must remain dependency-neutral,
but a non-network application should not need to link network libraries.

## 6. Roadmap

### Phase 0 — lock decisions and dependency boundary

- Confirm C11/platform support and the minimum libcurl version.
- Select yyjson or an equivalent parser and record license/version policy.
- Decide whether the JSON source is vendored or discovered through pkg-config.
- Define opaque handles, result ownership, error categories, and cancellation
  semantics before writing transport code.
- Add a tiny architecture/design test that links a current non-network target
  without libcurl.

**Exit criterion:** the dependency plan is documented and `make test` still
builds/links the existing core without any network library.

### Phase 1 — async lifecycle improvements

- Audit `vv_async` completion/result publication and cancellation behavior under
  C11 memory ordering.
- Add only the primitives networking needs: a safe completion/wakeup hook if
  useful, and clear non-blocking abandonment semantics or an internal wrapper.
- Add tests for completion, cancellation, progress, shutdown, and a worker that
  finishes while the UI is idle.
- Keep the existing public `vv_async_run` example source-compatible.

**Exit criterion:** no worker can call UI code, no result is read before
completion, and shutdown reliably joins every worker.

### Phase 2 — HTTP transport

- Implement `vv_Net`/`vv_NetRequest` and the libcurl easy-handle worker.
- Implement copied request inputs, headers, bounded body collection, timeouts,
  TLS verification defaults, status/error separation, and cancellation.
- Implement the thread-safe completion queue and low-level `vv_net_poll`.
- Add `vv_net_progress`, request state inspection, and explicit release APIs.

**Exit criterion:** a local fixture server can exercise successful GET/POST,
headers, binary bodies, 4xx/5xx, timeout, cancellation, and response-size
limits without a UI freeze or leak.

### Phase 3 — Verve message integration

- Implement `vv_net_dispatch(vv_Net *, vv_Ctx *)`.
- Verify that dispatch happens before `vv_run_frame`, completions become ordinary
  `vv_Event` messages, and `update` can safely release the request.
- Add the optional wake hook and document bounded waits for SDL/headless hosts.
- Add a small headless example showing loading, pending, success, error, cancel,
  and idle mode.
- Add a GUI example only after the headless example demonstrates the complete
  ownership and update/view pattern.

**Exit criterion:** the example never mutates state from a worker, rebuilds when
completion arrives in idle mode, and does not require a bespoke callback system.

### Phase 4 — JSON decoding

- Integrate the chosen external parser behind `vv_JsonDoc`/`vv_JsonValue`.
- Parse on the worker when requested, with a separate JSON error category.
- Implement object/array traversal and typed accessors, including null/type
  checks and string lengths.
- Test valid nested data, empty values, Unicode strings, numbers, malformed JSON,
  and a response containing embedded NUL bytes.
- Document that application state should copy/normalize the fields it needs;
  it should not retain a response forever as a general state store.

**Exit criterion:** a JSON API example can load a real nested response and render
it with no parser symbols visible in application code.

### Phase 5 — hardening and ergonomics

- Run ASan, UBSan, and ThreadSanitizer tests where available.
- Add tests for service destruction with active requests, release-before-complete,
  completion queue pressure, duplicate headers, redirects, and exact body limits.
- Review URL/header logging and sensitive-data handling.
- Add a small set of helpers only where examples show repeated ceremony: common
  GET, JSON accept header, status predicates, and header lookup.
- Update `README.md`, `GUIDE.md`, module layout, and `Makefile` documentation.

**Exit criterion:** the network module is optional, documented, testable offline,
and small enough that a user can understand its complete lifecycle from one
example.

### Phase 6 — evaluate, do not pre-commit

Only after real applications use the API, measure whether they need:

- a shared libcurl multi worker for connection reuse and many concurrent requests;
- streaming downloads/uploads;
- retry/backoff policies;
- ETag/cache helpers;
- a periodic refresh helper;
- better host-loop wake integration;
- WebSockets or server-sent events.

These should be separate additions or policies. They must not complicate the
basic request/result/message path.

## 7. Testing strategy

Tests must never depend on the public internet. Use a deterministic loopback
fixture server or a small test helper that can return:

- a normal JSON response;
- a delayed response for cancellation/timeout;
- a response with custom and duplicate headers;
- a binary body containing NUL bytes;
- a 404/500 response;
- a body that exceeds the configured limit;
- malformed JSON;
- a redirect when redirects are explicitly enabled.

Unit tests should cover request copying and ownership independently of the
transport where possible. Integration tests should assert that:

- no completion is dispatched twice;
- completion events are only emitted on the caller/UI thread;
- state is readable before request release and invalid afterward;
- cancellation does not leak the curl handle or response buffer;
- destroying the service while work is active is safe;
- core-only applications still link without curl;
- JSON parsing failures do not masquerade as HTTP failures;
- a response completion causes an idle Verve context to rebuild when dispatched.

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Worker thread touches `vv_Ctx` | Make dispatch main-thread-only and assert/document it. |
| Response lifetime is unclear | Immutable response, request-owned storage, explicit release, examples that copy data. |
| `vv_net_release` blocks the UI | Non-blocking release for pending requests; only service shutdown joins. |
| Idle mode misses completion | Dispatch before `vv_run_frame`; optional host wake hook; bounded fallback wait. |
| One thread per request scales poorly | Start simple; measure real workloads before introducing a multi worker. |
| HTTP status and transport errors are conflated | Separate status, transport code, cancellation, size, and JSON error fields. |
| Large/untrusted responses exhaust memory | Mandatory finite default size limit and early write-callback abort. |
| Dependency breaks core builds | Separate network archive/source list and a link test without curl. |
| JSON wrapper becomes a second parser | Keep it as a thin read-only adapter; no custom grammar or DOM implementation. |
| Automatic retries cause duplicate writes | No retries in v1; add explicit policy later. |
| Credentials leak into diagnostics | Never log URLs with credentials or header values by default. |

## 9. Definition of done for v1

The feature is ready when all of the following are true:

- A user can include the public header, start a request from `update`, show its
  state in `view`, and handle completion through a normal `vv_Event`.
- A network worker can never mutate Verve UI state directly.
- HTTP and JSON are delegated to maintained external libraries.
- Requests are cancellable, bounded, TLS-verified by default, and safe to abandon.
- Responses have explicit ownership and binary-safe bodies.
- JSON is parsed off the UI thread and exposed through a small dependency-neutral
  read-only API.
- Completion works in both custom loops and `vv_run_frame` applications.
- Idle-mode applications wake or poll in a documented, bounded way.
- Existing core builds/tests remain independent of optional network libraries.
- Tests are deterministic and cover lifecycle, errors, cancellation, ownership,
  thread boundaries, and malformed input.

The key design test is not whether the library can perform an HTTP request. It is
whether a Verve application can treat remote data as another asynchronous input
source while preserving the same simple rule it already has: **state changes in
`update`, UI is declared in `view`, and background work never reaches into the
UI thread.**
