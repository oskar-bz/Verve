// vv_async.h — run background work off the UI thread and fold the result back
// into update() when it's ready. The frame loop stays responsive; you poll the
// job each frame (cheap) and act on completion. Cooperative cancellation and a
// progress channel let the work report in and bail out.
//
//   App: Job *j = state->job = vv_async_run(load_file, path);
//   each update(): if (vv_async_done(j, &res)) { ...use res...; vv_async_free(j); }
//   work fn: for (...) { if (vv_async_cancelled(self)) return NULL;
//                        vv_async_set_progress(self, i/(float)n); }
#ifndef VV_ASYNC_H
#define VV_ASYNC_H

#include <stdbool.h>

typedef struct vv_Async vv_Async;
typedef void *(*vv_AsyncFn)(vv_Async *self, void *arg);

// Start `fn(self, arg)` on a background thread. Returns a handle; free it with
// vv_async_free (which joins the thread) once you've consumed the result.
vv_Async *vv_async_run(vv_AsyncFn fn, void *arg);

// Poll: true once the work returned; *result gets its return value (may be NULL).
bool  vv_async_done(vv_Async *a, void **result);
float vv_async_progress(vv_Async *a);         // 0..1, whatever the work reported

// From inside the work fn: report progress / check for a cancel request.
void  vv_async_set_progress(vv_Async *a, float p);
bool  vv_async_cancelled(vv_Async *a);

// Request cancellation (cooperative — the work must poll vv_async_cancelled).
void  vv_async_cancel(vv_Async *a);
// Join the thread and release the handle. Safe to call before completion (waits).
void  vv_async_free(vv_Async *a);

#endif // VV_ASYNC_H
