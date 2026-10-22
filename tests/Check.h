#pragma once

// Minimal test helpers: each failed check prints its location, main returns the count.

#include <cstdio>
#include <sstream>
#include <string>

inline int g_failures = 0;

template <typename A, typename B>
void CheckEqual(const A& actual, const B& expected, const char* actualText,
                const char* expectedText, const char* file, int line) {
    if (actual == expected) return;
    std::ostringstream a;
    std::ostringstream e;
    a << actual;
    e << expected;
    std::printf("%s:%d: %s == %s failed: got %s, expected %s\n", file, line, actualText,
                expectedText, a.str().c_str(), e.str().c_str());
    ++g_failures;
}

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

#define CHECK_EQ(actual, expected) \
    CheckEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)

inline int Finish(const char* name) {
    if (g_failures == 0) {
        std::printf("%s: OK\n", name);
        return 0;
    }
    std::printf("%s: %d failed\n", name, g_failures);
    return 1;
}
