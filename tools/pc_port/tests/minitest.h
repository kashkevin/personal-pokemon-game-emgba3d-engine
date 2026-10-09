// Minimal C test harness for the native PC platform layer (no dependencies).
// The game engine is C; a C++ framework (e.g. Catch2) would add a toolchain
// requirement for no benefit. Game mechanics are covered by the expansion's own
// test suite (`make check`, run in mGBA); these tests cover PC-only code.
#ifndef MINITEST_H
#define MINITEST_H
#include <stdio.h>
#include <string.h>

static int mt_failures, mt_checks;
static const char *mt_current;

#define MT_CHECK(cond) do { mt_checks++; if (!(cond)) { mt_failures++; \
    fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, mt_current, #cond); } } while (0)
#define MT_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); mt_checks++; \
    if (_a != _b) { mt_failures++; fprintf(stderr, "FAIL %s:%d [%s] %s == %s (%lld vs %lld)\n", \
    __FILE__, __LINE__, mt_current, #a, #b, _a, _b); } } while (0)
#define MT_TEST(name) static void name(void)
#define MT_RUN(name) do { mt_current = #name; name(); } while (0)
#define MT_REPORT() (printf("%d checks, %d failures\n", mt_checks, mt_failures), mt_failures ? 1 : 0)
#endif
