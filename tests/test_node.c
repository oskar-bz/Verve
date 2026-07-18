#include "verve/vv_arena.h"
#include "verve/vv_node.h"
#include "vv_test.h"

static void run_tests(void) {
    vv_Arena arena;
    vv_arena_init(&arena, 1 << 16);
    vv_NodePool p;
    vv_pool_init(&p, &arena, 16);

    // Obtain is idempotent per ID.
    bool created = false;
    uint32_t a = vv_pool_obtain(&p, 1001, &created);
    CHECK(created);
    uint32_t a2 = vv_pool_obtain(&p, 1001, &created);
    CHECK(!created);
    CHECK_EQ_U(a, a2);

    CHECK_EQ_U(vv_pool_find(&p, 1001), a);
    CHECK_EQ_U(vv_pool_find(&p, 9999), VV_NIL);

    // Free returns the slot to the freelist; next obtain reuses it.
    vv_pool_free(&p, a);
    CHECK_EQ_U(vv_pool_find(&p, 1001), VV_NIL);
    uint32_t b = vv_pool_obtain(&p, 2002, &created);
    CHECK(created);
    CHECK_EQ_U(b, a); // reused slot

    // Stress: churn many IDs, force pool + map growth, assert no collisions
    // and correct lookup.
    for (uint32_t i = 0; i < 5000; i++) {
        uint32_t idx = vv_pool_obtain(&p, 100000 + i, &created);
        CHECK(created);
        vv_pool_get(&p, idx)->child_count = i; // tag it
    }
    for (uint32_t i = 0; i < 5000; i++) {
        uint32_t idx = vv_pool_find(&p, 100000 + i);
        CHECK(idx != VV_NIL);
        CHECK_EQ_U(vv_pool_get(&p, idx)->child_count, i);
    }
    // Delete half, verify the rest still resolve (probe-chain integrity).
    for (uint32_t i = 0; i < 5000; i += 2) vv_pool_free(&p, vv_pool_find(&p, 100000 + i));
    for (uint32_t i = 1; i < 5000; i += 2) {
        uint32_t idx = vv_pool_find(&p, 100000 + i);
        CHECK(idx != VV_NIL);
        CHECK_EQ_U(vv_pool_get(&p, idx)->child_count, i);
    }
    for (uint32_t i = 0; i < 5000; i += 2) CHECK_EQ_U(vv_pool_find(&p, 100000 + i), VV_NIL);

    vv_arena_destroy(&arena);
}

TEST_MAIN()
