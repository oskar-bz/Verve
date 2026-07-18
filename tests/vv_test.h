// Minimal test harness. Each test file has a main() that runs CHECKs.
#ifndef VV_TEST_H
#define VV_TEST_H
#include <stdio.h>
#include <stdlib.h>

static int vv_test_fails = 0;

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
        vv_test_fails++;                                                   \
    }                                                                      \
} while (0)

#define CHECK_EQ_U(a, b) do {                                              \
    unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
    if (_a != _b) {                                                        \
        printf("  FAIL %s:%d  %s == %s (%llu vs %llu)\n",                  \
               __FILE__, __LINE__, #a, #b, _a, _b);                       \
        vv_test_fails++;                                                   \
    }                                                                      \
} while (0)

#define CHECK_NEAR(a, b, eps) do {                                         \
    double _a = (double)(a), _b = (double)(b);                            \
    double _d = _a - _b; if (_d < 0) _d = -_d;                            \
    if (_d > (eps)) {                                                      \
        printf("  FAIL %s:%d  %s ~= %s (%f vs %f)\n",                      \
               __FILE__, __LINE__, #a, #b, _a, _b);                       \
        vv_test_fails++;                                                   \
    }                                                                      \
} while (0)

#define TEST_MAIN() \
    int main(void) { run_tests(); \
        if (vv_test_fails) { printf("  %d check(s) failed\n", vv_test_fails); return 1; } \
        printf("  ok\n"); return 0; }

#endif
