#include "verve/vv_arena.h"
#include "vv_test.h"
#include <string.h>

static void run_tests(void) {
    vv_Arena a;
    vv_arena_init(&a, 4096);

    // Alignment.
    char  *p1 = vv_arena_alloc_aligned(&a, 1, 1);
    double *p2 = vv_arena_alloc_aligned(&a, sizeof(double), 16);
    CHECK(((size_t)p2 % 16) == 0);
    *p1 = 7; *p2 = 3.14;
    CHECK(*p1 == 7);

    // calloc zeroes.
    int *z = vv_arena_calloc(&a, sizeof(int) * 32);
    for (int i = 0; i < 32; i++) CHECK(z[i] == 0);

    // Growth beyond initial block, and oversized allocation.
    size_t before = a.total_reserved;
    void *big = vv_arena_alloc(&a, 1 << 20);
    CHECK(big != NULL);
    CHECK(a.total_reserved > before);
    memset(big, 0xAB, 1 << 20);

    // Reset reuses memory: pointer of first alloc after reset matches earlier.
    vv_arena_reset(&a);
    char *r1 = vv_arena_alloc_aligned(&a, 1, 1);
    CHECK(r1 == p1); // same block, same cursor
    CHECK(a.total_used == 1);

    vv_arena_destroy(&a);
}

TEST_MAIN()
