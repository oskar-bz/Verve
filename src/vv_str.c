#include "verve/vv_str.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// Allocate a fat string: [header][cap bytes][NUL]. The header sits immediately
// before the returned data so the handle stays a plain char* (§13.x). Arena
// memory is max_align_t-aligned, and vv_StrHeader is 8 bytes, so the data that
// follows is aligned too — irrelevant for char but keeps the header aligned.
vv_Str vv_str_reserve(vv_Arena *a, size_t cap) {
    vv_StrHeader *h = vv_arena_alloc(a, sizeof(vv_StrHeader) + cap + 1);
    h->len = (uint32_t)cap;
    h->cap = (uint32_t)cap;
    char *data = (char *)(h + 1);
    data[cap] = 0;
    return data;
}

size_t vv_cstr_len(const char *s) { return s ? strlen(s) : 0; }

vv_Str vv_str_from(vv_Arena *a, const char *bytes, size_t len) {
    vv_Str s = vv_str_reserve(a, len);
    if (len && bytes) memcpy(s, bytes, len);
    s[len] = 0;
    vv_str_header(s)->len = (uint32_t)len;
    return s;
}

vv_Str vv_str_dup(vv_Arena *a, const char *cstr) {
    return vv_str_from(a, cstr, vv_cstr_len(cstr));
}

vv_Str vv_str_cat(vv_Arena *a, const char *x, const char *y) {
    size_t lx = vv_cstr_len(x), ly = vv_cstr_len(y);
    vv_Str s = vv_str_reserve(a, lx + ly);
    if (lx) memcpy(s, x, lx);
    if (ly) memcpy(s + lx, y, ly);
    s[lx + ly] = 0;
    return s;
}

vv_Str vv_str_sub(vv_Arena *a, const char *s, size_t start, size_t len) {
    size_t n = vv_cstr_len(s);
    if (start > n) start = n;
    if (start + len > n) len = n - start;
    return vv_str_from(a, s + start, len);
}

vv_Str vv_str_trim(vv_Arena *a, const char *s) {
    size_t n = vv_cstr_len(s);
    size_t i = 0, j = n;
    while (i < j && isspace((unsigned char)s[i])) i++;
    while (j > i && isspace((unsigned char)s[j - 1])) j--;
    return vv_str_from(a, s + i, j - i);
}

vv_Str vv_str_lower(vv_Arena *a, const char *s) {
    vv_Str r = vv_str_dup(a, s);
    for (size_t i = 0, n = vv_str_len(r); i < n; i++) r[i] = (char)tolower((unsigned char)r[i]);
    return r;
}

vv_Str vv_str_upper(vv_Arena *a, const char *s) {
    vv_Str r = vv_str_dup(a, s);
    for (size_t i = 0, n = vv_str_len(r); i < n; i++) r[i] = (char)toupper((unsigned char)r[i]);
    return r;
}

vv_Str vv_str_vformat(vv_Arena *a, const char *fmt, va_list ap) {
    va_list cp;
    va_copy(cp, ap);
    int need = vsnprintf(NULL, 0, fmt, cp); // dry run to size exactly
    va_end(cp);
    if (need < 0) return vv_str_from(a, "", 0);
    vv_Str s = vv_str_reserve(a, (size_t)need);
    vsnprintf(s, (size_t)need + 1, fmt, ap);
    vv_str_header(s)->len = (uint32_t)need;
    return s;
}

vv_Str vv_str_format(vv_Arena *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vv_Str s = vv_str_vformat(a, fmt, ap);
    va_end(ap);
    return s;
}

vv_Str *vv_str_split(vv_Arena *a, const char *s, char sep, size_t *count) {
    size_t n = vv_cstr_len(s);
    size_t parts = 1;
    for (size_t i = 0; i < n; i++) if (s[i] == sep) parts++;

    vv_Str *out = VV_ARENA_NEW_N(a, vv_Str, parts);
    size_t p = 0, start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == sep) {
            out[p++] = vv_str_from(a, s + start, i - start);
            start = i + 1;
        }
    }
    if (count) *count = parts;
    return out;
}

vv_Str vv_str_join(vv_Arena *a, const char *const *parts, size_t n, const char *sep) {
    size_t sl = vv_cstr_len(sep), total = 0;
    for (size_t i = 0; i < n; i++) total += vv_cstr_len(parts[i]);
    if (n > 1) total += sl * (n - 1);

    vv_Str s = vv_str_reserve(a, total);
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
        if (i && sl) { memcpy(s + at, sep, sl); at += sl; }
        size_t pl = vv_cstr_len(parts[i]);
        if (pl) { memcpy(s + at, parts[i], pl); at += pl; }
    }
    s[at] = 0;
    vv_str_header(s)->len = (uint32_t)at;
    return s;
}

bool vv_str_eq(const char *x, const char *y) {
    if (x == y) return true;
    if (!x || !y) return false;
    return strcmp(x, y) == 0;
}

bool vv_str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    size_t lp = strlen(prefix);
    return strncmp(s, prefix, lp) == 0;
}

bool vv_str_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t ls = strlen(s), lx = strlen(suffix);
    return lx <= ls && memcmp(s + ls - lx, suffix, lx) == 0;
}

ptrdiff_t vv_str_find(const char *s, const char *needle) {
    if (!s || !needle) return -1;
    const char *hit = strstr(s, needle);
    return hit ? (ptrdiff_t)(hit - s) : -1;
}

// ---- UTF-8 -----------------------------------------------------------------

uint32_t vv_utf8_decode(const char *s, int *adv) {
    const unsigned char *u = (const unsigned char *)s;
    unsigned char c = u[0];
    if (c < 0x80) { if (adv) *adv = 1; return c; }
    if ((c & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        if (adv) *adv = 2;
        return (uint32_t)((c & 0x1F) << 6) | (u[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        if (adv) *adv = 3;
        return (uint32_t)((c & 0x0F) << 12) | (uint32_t)((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        if (adv) *adv = 4;
        return (uint32_t)((c & 0x07) << 18) | (uint32_t)((u[1] & 0x3F) << 12) |
               (uint32_t)((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    }
    if (adv) *adv = 1; // malformed: consume one byte
    return 0xFFFD;
}

int vv_utf8_next(const char *s, int len, int i) {
    if (i >= len) return len;
    int adv = 1;
    vv_utf8_decode(s + i, &adv);
    i += adv;
    return i > len ? len : i;
}

int vv_utf8_prev(const char *s, int i) {
    if (i <= 0) return 0;
    i--;
    // Back up over UTF-8 continuation bytes (10xxxxxx).
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

int vv_utf8_count(const char *s, int len) {
    int n = 0;
    for (int i = 0; i < len;) { i = vv_utf8_next(s, len, i); n++; }
    return n;
}

int vv_utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
