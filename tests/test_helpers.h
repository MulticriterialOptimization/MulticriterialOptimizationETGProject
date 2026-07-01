#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <cassert>
#include <iostream>
#include <cmath>
#include <string>

#define TEST(name) static void name()

#define RUN(name) do { \
    std::cout << "  " #name "..."; \
    name(); \
    std::cout << " OK\n"; \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) { \
        std::cerr << "\n    FAIL: " << #a << " == " << #b \
                  << " (" << (a) << " vs " << (b) << ")" \
                  << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        assert(false); \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (!(std::abs((a)-(b)) < (eps))) { \
        std::cerr << "\n    FAIL: |" << #a << " - " << #b << "| < " << (eps) \
                  << " (" << (a) << " vs " << (b) << ", diff=" << std::abs((a)-(b)) << ")" \
                  << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        assert(false); \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        std::cerr << "\n    FAIL: " << #x \
                  << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        assert(false); \
    } \
} while(0)

#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))

#endif
