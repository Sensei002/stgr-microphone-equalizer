// Minimal test framework for STGR.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace stgr::test {

static int g_passed = 0, g_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        ++::stgr::test::g_failed; \
        printf("FAIL: %s - %s (line %d)\n", msg, #cond, __LINE__); \
    } else { \
        ++::stgr::test::g_passed; \
    } \
} while(0)

#define CHECK_CLOSE(a, b, eps, msg) do { \
    const double diff = std::fabs((double)(a) - (double)(b)); \
    if (diff > (eps)) { \
        ++::stgr::test::g_failed; \
        printf("FAIL: %s - expected %.15g got %.15g (diff=%g) line %d\n", msg, (double)(b), (double)(a), diff, __LINE__); \
    } else { \
        ++::stgr::test::g_passed; \
    } \
} while(0)

inline int test_summary()
{
    printf("\nTests: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}

} // namespace stgr::test