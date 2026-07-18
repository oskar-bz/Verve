#include "verve/vv_id.h"

#include <string.h>

// FNV-1a 64-bit. Cheap, decent distribution, no dependency. The ID space is
// large enough that collisions are ignorable; debug builds assert on them in
// the node map, not here.
#define FNV_PRIME 0x100000001b3ULL

static vv_ID fnv_mix(vv_ID h, const void *data, size_t len) {
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

vv_ID vv_id(vv_ID parent, uint32_t seq, const char *key, size_t klen) {
    vv_ID h = parent ? parent : VV_ID_ROOT;
    // An explicit key REPLACES the sequence index as the discriminator, so a
    // keyed widget keeps its identity even when conditional siblings shift its
    // position (§3.1). Keyless widgets fall back to sequence.
    if (key && klen) h = fnv_mix(h, key, klen);
    else             h = fnv_mix(h, &seq, sizeof(seq));
    // Guard: never return 0 — the node map uses 0 as an empty-slot sentinel.
    return h ? h : VV_ID_ROOT;
}

vv_ID vv_id_str(vv_ID parent, uint32_t seq, const char *key) {
    return vv_id(parent, seq, key, key ? strlen(key) : 0);
}
