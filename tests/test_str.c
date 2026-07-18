#include "verve/vv_str.h"
#include "vv_test.h"
#include <string.h>

static void run_tests(void) {
    vv_Arena a; vv_arena_init(&a, 1 << 16);

    // Fat pointer: NUL-terminated + O(1) length via the header.
    vv_Str s = vv_str_dup(&a, "hello");
    CHECK(vv_str_len(s) == 5);
    CHECK(strcmp(s, "hello") == 0);          // drops into C APIs unchanged
    CHECK(s[5] == 0);

    // from / sub / cat.
    vv_Str h = vv_str_from(&a, "hello world", 5);
    CHECK(vv_str_len(h) == 5 && strcmp(h, "hello") == 0);
    vv_Str sub = vv_str_sub(&a, "hello world", 6, 5);
    CHECK(strcmp(sub, "world") == 0);
    vv_Str both = vv_str_cat(&a, "foo", "bar");
    CHECK(strcmp(both, "foobar") == 0 && vv_str_len(both) == 6);

    // Embedded NUL survives (fat pointer tracks length independently).
    vv_Str emb = vv_str_from(&a, "a\0b", 3);
    CHECK(vv_str_len(emb) == 3 && emb[1] == 0 && emb[2] == 'b');

    // format — the widget-formatting replacement.
    vv_Str f = vv_str_format(&a, "%s = %.1f%%", "load", 42.25);
    CHECK(strcmp(f, "load = 42.2%") == 0);
    CHECK(vv_str_len(f) == strlen(f));       // header length matches content

    // A long format forces exact sizing past any small-buffer guess.
    vv_Str big = vv_str_format(&a, "%0300d", 7);
    CHECK(vv_str_len(big) == 300);

    // split preserves empty fields.
    size_t n = 0;
    vv_Str *parts = vv_str_split(&a, "a,,b,", ',', &n);
    CHECK(n == 4);
    CHECK(strcmp(parts[0], "a") == 0);
    CHECK(strcmp(parts[1], "") == 0);
    CHECK(strcmp(parts[2], "b") == 0);
    CHECK(strcmp(parts[3], "") == 0);

    // join round-trips a split (minus the empties choice).
    const char *words[] = { "one", "two", "three" };
    vv_Str joined = vv_str_join(&a, words, 3, ", ");
    CHECK(strcmp(joined, "one, two, three") == 0);

    // trim / case.
    CHECK(strcmp(vv_str_trim(&a, "  hi \t\n"), "hi") == 0);
    CHECK(strcmp(vv_str_upper(&a, "aBc"), "ABC") == 0);
    CHECK(strcmp(vv_str_lower(&a, "aBc"), "abc") == 0);

    // search / predicates.
    CHECK(vv_str_eq("x", "x") && !vv_str_eq("x", "y"));
    CHECK(vv_str_starts_with("verve", "ver"));
    CHECK(vv_str_ends_with("verve", "rve"));
    CHECK(vv_str_find("abcdef", "cd") == 2);
    CHECK(vv_str_find("abcdef", "zz") == -1);
    CHECK(vv_str_contains("hello world", "o w"));

    vv_arena_destroy(&a);
}

TEST_MAIN()
