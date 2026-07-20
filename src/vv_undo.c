// vv_undo.c — snapshot undo/redo ring (see vv_undo.h).
#include "verve/vv_undo.h"

#include <stdlib.h>
#include <string.h>

static unsigned char *slot(vv_History *h, int i) {
    return h->slots + (size_t)((h->head + i) % h->cap) * (size_t)h->elem;
}

void vv_history_init(vv_History *h, int elem, int cap) {
    if (cap < 2) cap = 2;
    h->elem = elem;
    h->cap = cap;
    h->slots = calloc((size_t)cap, (size_t)elem);
    h->head = h->len = h->cur = 0;
}

void vv_history_free(vv_History *h) {
    free(h->slots);
    h->slots = NULL;
    h->len = h->cur = 0;
}

void vv_history_push(vv_History *h, const void *state) {
    // No-op if identical to the current snapshot.
    if (h->len > 0 && memcmp(slot(h, h->cur), state, (size_t)h->elem) == 0) return;

    // Drop any redo tail (everything after cur).
    h->len = h->cur + 1;

    if (h->len == h->cap) {
        // Full: drop the oldest so the newest fits.
        h->head = (h->head + 1) % h->cap;
        h->len--;
    }
    memcpy(slot(h, h->len), state, (size_t)h->elem);
    h->len++;
    h->cur = h->len - 1;
}

bool vv_history_can_undo(const vv_History *h) { return h->cur > 0; }
bool vv_history_can_redo(const vv_History *h) { return h->cur < h->len - 1; }

bool vv_history_undo(vv_History *h, void *out) {
    if (!vv_history_can_undo(h)) return false;
    h->cur--;
    memcpy(out, slot(h, h->cur), (size_t)h->elem);
    return true;
}

bool vv_history_redo(vv_History *h, void *out) {
    if (!vv_history_can_redo(h)) return false;
    h->cur++;
    memcpy(out, slot(h, h->cur), (size_t)h->elem);
    return true;
}
