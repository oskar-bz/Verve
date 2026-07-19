#include "verve/verve.h"
#include "vv_test.h"
int main(void) {
    // "e" + combining acute (U+0301) is ONE grapheme (é).
    const char *e_acute = "e\xcc\x81x"; // e, U+0301, x  => 4 bytes
    CHECK(vv_grapheme_next(e_acute, 4, 0) == 3); // skips e + combining mark
    CHECK(vv_grapheme_next(e_acute, 4, 3) == 4); // then x
    CHECK(vv_grapheme_prev(e_acute, 4, 4) == 3);
    CHECK(vv_grapheme_prev(e_acute, 4, 3) == 0); // back over the whole cluster

    // A regional-indicator flag pair (🇬🇧) is ONE grapheme (8 bytes).
    const char *flag = "\xf0\x9f\x87\xac\xf0\x9f\x87\xa7"; // U+1F1EC U+1F1E7
    CHECK(vv_grapheme_next(flag, 8, 0) == 8);
    CHECK(vv_grapheme_prev(flag, 8, 8) == 0);

    // Plain ASCII behaves like codepoints.
    CHECK(vv_grapheme_next("abc", 3, 1) == 2);
    if (vv_test_fails) { printf("FAILED (%d)\n", vv_test_fails); return 1; }
    printf("  ok\n"); return 0;
}
