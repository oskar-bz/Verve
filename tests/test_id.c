#include "verve/vv_id.h"
#include "vv_test.h"

static void run_tests(void) {
    // Determinism.
    CHECK(vv_id(1, 0, NULL, 0) == vv_id(1, 0, NULL, 0));

    // Sequence index changes identity.
    CHECK(vv_id(1, 0, NULL, 0) != vv_id(1, 1, NULL, 0));

    // Parent scoping: same seq under different parents differ.
    CHECK(vv_id(1, 0, NULL, 0) != vv_id(2, 0, NULL, 0));

    // Explicit key stabilizes across seq shifts (§3.1): same key, any seq,
    // same identity.
    CHECK_EQ_U(vv_id_str(1, 0, "ok"), vv_id_str(1, 5, "ok"));
    // Different-key siblings are distinct:
    CHECK(vv_id_str(1, 0, "a") != vv_id_str(1, 0, "b"));

    // Never zero (map sentinel).
    CHECK(vv_id(0, 0, NULL, 0) != 0);
}

TEST_MAIN()
