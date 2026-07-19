// vv_str.h — fat-pointer strings (§13.x).
//
// The handle is a plain `char *`: NUL-terminated, so a vv_Str drops straight
// into any C API (printf, the text widget, strcmp) with no conversion. A small
// header stashed immediately *before* the data holds length + capacity, so
// length is O(1) and slicing is cheap. This is the sds/stb-style "fat pointer".
//
//   header (8B)          data ->
//   [ len | cap ] [ b y t e s ... \0 ]
//                  ^ vv_Str points here
//
// Every constructor allocates from a caller-supplied arena, so lifetime follows
// the arena — no free, no ownership bookkeeping (§13). Use the frame arena for
// per-frame text (labels, formatted values) and the persistent arena for text
// that must outlive the frame. Strings are treated as immutable: operations
// return fresh strings rather than mutating in place (an arena can't realloc).
//
// str_format is the point of the whole thing for UI code: it replaces ad-hoc
// snprintf-into-a-stack-buffer at every label. `vv_label(c, vv_fmt(c, "%.1f s",
// t))` needs no scratch buffer and no length guessing.
#ifndef VV_STR_H
#define VV_STR_H

#include "vv_arena.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef char *vv_Str;

typedef struct {
    uint32_t len;
    uint32_t cap; // bytes of data capacity, excluding the NUL
} vv_StrHeader;

// ---- introspection (work on any NUL-terminated char*, fat or not) ----------

// O(1) for a fat pointer built by this module. For a foreign C string the
// header is meaningless, so use vv_cstr_len / strlen there instead.
static inline vv_StrHeader *vv_str_header(vv_Str s) {
    return &((vv_StrHeader *)s)[-1];
}
static inline size_t vv_str_len(vv_Str s) { return s ? vv_str_header(s)->len : 0; }
size_t vv_cstr_len(const char *s); // strlen, NULL-safe

// ---- UTF-8 (§10) -----------------------------------------------------------
// Text is UTF-8 throughout. Storage stays byte-indexed; these let callers step
// and count by codepoint so editing lands on character boundaries, not mid-byte.

// Decode the codepoint at `s`; `*adv` (nullable) gets the byte length (1..4).
// Returns U+FFFD and adv=1 on a malformed lead/continuation byte.
uint32_t vv_utf8_decode(const char *s, int *adv);
// Byte index of the next / previous codepoint boundary around byte index `i`
// within a buffer of `len` bytes (clamped to [0,len]).
int      vv_utf8_next(const char *s, int len, int i);
int      vv_utf8_prev(const char *s, int i);
// Number of codepoints in the first `len` bytes.
int      vv_utf8_count(const char *s, int len);
// Encode `cp` into `out` (needs 4 bytes); returns bytes written (1..4).
int      vv_utf8_encode(uint32_t cp, char *out);

// Grapheme cluster boundaries (a reduced UAX #29): a base plus its combining
// marks / variation selectors, ZWJ-joined emoji, and regional-indicator flag
// pairs count as ONE character — so one arrow or backspace moves/deletes one
// *perceived* character. Byte indices into a `len`-byte buffer, clamped.
int      vv_grapheme_next(const char *s, int len, int i);
int      vv_grapheme_prev(const char *s, int len, int i);

// ---- construction ----------------------------------------------------------

// Uninitialized data of `cap` bytes (+NUL). len is set to `cap`; callers that
// fill fewer bytes should adjust via the header. Mostly an internal building
// block, exposed for widgets that format directly into the buffer.
vv_Str vv_str_reserve(vv_Arena *a, size_t cap);

vv_Str vv_str_from(vv_Arena *a, const char *bytes, size_t len);
vv_Str vv_str_dup(vv_Arena *a, const char *cstr); // from a NUL-terminated string
vv_Str vv_str_cat(vv_Arena *a, const char *x, const char *y);
vv_Str vv_str_sub(vv_Arena *a, const char *s, size_t start, size_t len);
vv_Str vv_str_trim(vv_Arena *a, const char *s); // strip leading/trailing ASCII ws
vv_Str vv_str_lower(vv_Arena *a, const char *s);
vv_Str vv_str_upper(vv_Arena *a, const char *s);

// printf-style formatting into a fresh arena string. Exact-sized (measured with
// a dry vsnprintf, then allocated). This is what lets the text widget stay
// dumb: all interpolation happens here, the widget only ever draws bytes.
vv_Str vv_str_format(vv_Arena *a, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
vv_Str vv_str_vformat(vv_Arena *a, const char *fmt, va_list ap);

// ---- splitting / joining ---------------------------------------------------

// Split on a single-byte separator. Returns a heap-free array (arena-allocated)
// of `*count` fat strings; empty fields are preserved ("a,,b" -> "a","","b").
vv_Str *vv_str_split(vv_Arena *a, const char *s, char sep, size_t *count);

// Join `n` C strings with `sep` between them.
vv_Str vv_str_join(vv_Arena *a, const char *const *parts, size_t n, const char *sep);

// ---- comparison / search (fat-pointer not required) ------------------------

bool   vv_str_eq(const char *x, const char *y);
bool   vv_str_starts_with(const char *s, const char *prefix);
bool   vv_str_ends_with(const char *s, const char *suffix);
// Byte index of first occurrence of `needle`, or -1.
ptrdiff_t vv_str_find(const char *s, const char *needle);
static inline bool vv_str_contains(const char *s, const char *needle) {
    return vv_str_find(s, needle) >= 0;
}

#endif // VV_STR_H
