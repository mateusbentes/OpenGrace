#pragma once

#include <cstdio>
#include <string>

namespace grace::test {

inline int g_checks = 0;
inline int g_failures = 0;

inline void report(const char* file, int line, const std::string& message)
{
    ++g_failures;
    std::fprintf(stderr, "%s:%d: FAIL %s\n", file, line, message.c_str());
}

inline int summary(const char* suite)
{
    std::printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace grace::test

#define GRACE_CHECK(cond)                                                  \
    do {                                                                   \
        ++::grace::test::g_checks;                                         \
        if (!(cond)) {                                                     \
            ::grace::test::report(__FILE__, __LINE__, "!(" #cond ")");     \
        }                                                                  \
    } while (false)

#define GRACE_CHECK_EQ(a, b)                                               \
    do {                                                                   \
        ++::grace::test::g_checks;                                         \
        if (!((a) == (b))) {                                               \
            ::grace::test::report(__FILE__, __LINE__, #a " == " #b);       \
        }                                                                  \
    } while (false)
