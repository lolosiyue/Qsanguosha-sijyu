#pragma once

#include <cstdio>
#include <cstdlib>

namespace card_lifetime_test {
[[noreturn]] inline void fail(const char *expression, const char *file, int line)
{
    std::fprintf(stderr, "FAIL %s (%s:%d)\n", expression, file, line);
    std::abort();
}

inline void check(bool condition, const char *expression, const char *file, int line)
{
    if (!condition)
        fail(expression, file, line);
}
}

#define CARD_LIFETIME_CHECK(expression) do { \
    const bool cardLifetimeCheckResult = static_cast<bool>(expression); \
    ::card_lifetime_test::check(cardLifetimeCheckResult, #expression, __FILE__, __LINE__); \
} while (false)
