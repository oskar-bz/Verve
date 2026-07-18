// vv_id.h — parent-scoped identity derivation (§3.1).
//
// An ID is hash(parent_id, child_sequence_index, optional_user_key). This is
// deliberately NOT call-site based so C and Lua behave identically. Users add
// an explicit key to any widget that can appear conditionally or reorder.
#ifndef VV_ID_H
#define VV_ID_H

#include "vv_types.h"

#define VV_ID_ROOT ((vv_ID)0xcbf29ce484222325ULL) // FNV-1a offset basis

// Derive a child ID. `key` may be NULL (klen ignored) to rely on sequence.
vv_ID vv_id(vv_ID parent, uint32_t seq, const char *key, size_t klen);

// Convenience: NUL-terminated key.
vv_ID vv_id_str(vv_ID parent, uint32_t seq, const char *key);

#endif // VV_ID_H
