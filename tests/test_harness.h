#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ── Global counters ── */
static int _test_pass_count = 0;
static int _test_fail_count = 0;

/* ── Test declaration macro ── */
#define TEST(name) \
    static void test_##name(void); \
    static void test_##name(void)

/* ── Assertions ── */
#define ASSERT_EQ(a, b) do {                                          \
    if ((a) != (b)) {                                                 \
        printf("  FAIL: %s:%d: %s != %s\n",                          \
               __FILE__, __LINE__, #a, #b);                           \
        _test_fail_count++;                                           \
        return;                                                       \
    }                                                                 \
} while (0)

#define ASSERT_NEQ(a, b) do {                                         \
    if ((a) == (b)) {                                                 \
        printf("  FAIL: %s:%d: %s == %s (expected different)\n",      \
               __FILE__, __LINE__, #a, #b);                           \
        _test_fail_count++;                                           \
        return;                                                       \
    }                                                                 \
} while (0)

#define ASSERT_TRUE(x) do {                                           \
    if (!(x)) {                                                       \
        printf("  FAIL: %s:%d: %s is false\n",                        \
               __FILE__, __LINE__, #x);                               \
        _test_fail_count++;                                           \
        return;                                                       \
    }                                                                 \
} while (0)

#define ASSERT_NEAR(a, b, eps) do {                                   \
    if (fabs((double)(a) - (double)(b)) > (eps)) {                    \
        printf("  FAIL: %s:%d: |%s - %s| > %s  (%.6f vs %.6f)\n",    \
               __FILE__, __LINE__, #a, #b, #eps,                      \
               (double)(a), (double)(b));                              \
        _test_fail_count++;                                           \
        return;                                                       \
    }                                                                 \
} while (0)

#define TEST_PASS() do {                                              \
    _test_pass_count++;                                               \
} while (0)

#define TEST_FAIL(msg) do {                                           \
    printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);          \
    _test_fail_count++;                                               \
    return;                                                           \
} while (0)

/* ── Runner helpers ── */
#define RUN_TEST(name) do {                                           \
    printf("  [RUN ] test_%s\n", #name);                              \
    int _before_fail = _test_fail_count;                              \
    test_##name();                                                    \
    if (_test_fail_count == _before_fail) {                           \
        _test_pass_count++;                                           \
        printf("  [ OK ] test_%s\n", #name);                         \
    } else {                                                          \
        printf("  [FAIL] test_%s\n", #name);                         \
    }                                                                 \
} while (0)

#define TEST_SUMMARY() do {                                           \
    printf("\n── Results: %d passed, %d failed ──\n",                 \
           _test_pass_count, _test_fail_count);                       \
    return _test_fail_count > 0 ? 1 : 0;                             \
} while (0)

#endif /* TEST_HARNESS_H */
