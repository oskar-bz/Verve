// vv_async.c — background jobs over pthreads (see vv_async.h).
#include "verve/vv_async.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

struct vv_Async {
    pthread_t       thread;
    vv_AsyncFn      fn;
    void           *arg;
    void           *result;
    atomic_bool     done;
    atomic_bool     cancel;
    _Atomic float   progress;
    bool            joined;
};

static void *trampoline(void *p) {
    vv_Async *a = p;
    a->result = a->fn(a, a->arg);
    atomic_store(&a->done, true);
    return NULL;
}

vv_Async *vv_async_run(vv_AsyncFn fn, void *arg) {
    vv_Async *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->fn = fn;
    a->arg = arg;
    atomic_store(&a->done, false);
    atomic_store(&a->cancel, false);
    atomic_store(&a->progress, 0.0f);
    if (pthread_create(&a->thread, NULL, trampoline, a) != 0) { free(a); return NULL; }
    return a;
}

bool vv_async_done(vv_Async *a, void **result) {
    if (!a || !atomic_load(&a->done)) return false;
    if (result) *result = a->result;
    return true;
}

float vv_async_progress(vv_Async *a) { return a ? atomic_load(&a->progress) : 0.0f; }
void  vv_async_set_progress(vv_Async *a, float p) { if (a) atomic_store(&a->progress, p); }
bool  vv_async_cancelled(vv_Async *a) { return a && atomic_load(&a->cancel); }
void  vv_async_cancel(vv_Async *a) { if (a) atomic_store(&a->cancel, true); }

void vv_async_free(vv_Async *a) {
    if (!a) return;
    if (!a->joined) { pthread_join(a->thread, NULL); a->joined = true; }
    free(a);
}
