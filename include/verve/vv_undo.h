// vv_undo.h — a generic snapshot undo/redo history.
//
// Value-snapshot model: the app calls vv_history_push() with a copy of whatever
// state should be undoable (a text buffer + cursor, a document struct, …); undo
// and redo hand back earlier/later snapshots. Simple and robust — no per-edit
// command objects — and a good fit for small documents. Snapshot on commit
// boundaries (word breaks, blur) rather than every keystroke to keep it light.
#ifndef VV_UNDO_H
#define VV_UNDO_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned char *slots; // cap * elem contiguous snapshots (ring)
    int   elem;           // bytes per snapshot
    int   cap;            // ring capacity
    int   head;           // ring index of the oldest live snapshot
    int   len;            // live snapshots
    int   cur;            // 0..len-1: position within history (len-1 = newest)
} vv_History;

// `elem` bytes per snapshot, `cap` snapshots retained (older ones drop off).
void vv_history_init(vv_History *h, int elem, int cap);
void vv_history_free(vv_History *h);

// Record `state` as the new newest snapshot. Truncates any redo tail first (a
// fresh edit after undo discards the redone future), then drops the oldest if
// full. Skips the push if `state` is byte-identical to the current snapshot.
void vv_history_push(vv_History *h, const void *state);

bool vv_history_can_undo(const vv_History *h);
bool vv_history_can_redo(const vv_History *h);

// Move one step back/forward and copy that snapshot into `out`. Returns false
// (leaving `out` untouched) at the ends of the history.
bool vv_history_undo(vv_History *h, void *out);
bool vv_history_redo(vv_History *h, void *out);

#endif // VV_UNDO_H
