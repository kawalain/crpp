#pragma once
// Minimal test support: no framework dependency. A failed CHECK is recorded
// and the process exits non-zero from crpp_test_result().
#include <cstdio>

namespace crpp::test {
inline int failures = 0;
}

#define CHECK(expr)                                                          \
  do {                                                                       \
    if (!(expr)) {                                                           \
      ++::crpp::test::failures;                                              \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                   #expr);                                                   \
    }                                                                        \
  } while (false)

inline int crpp_test_result() { return ::crpp::test::failures == 0 ? 0 : 1; }
