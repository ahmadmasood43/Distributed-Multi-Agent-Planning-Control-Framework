#include "mapc/common/uint128.hpp"

#include "mapc/common/rng.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace mapc {
namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

static_assert(UInt128::multiply(kMax, kMax) == UInt128{kMax - 1, 1});
static_assert(UInt128::multiply(0, kMax) == UInt128{0, 0});
static_assert(UInt128{0, kMax} + UInt128{0, 1} == UInt128{1, 0});

TEST(UInt128Test, MultiplicationEdgeCases) {
    EXPECT_EQ(UInt128::multiply(1, kMax), (UInt128{0, kMax}));
    EXPECT_EQ(UInt128::multiply(1ULL << 32U, 1ULL << 32U), (UInt128{1, 0}));
    EXPECT_EQ(UInt128::multiply(kMax, 2), (UInt128{1, kMax - 1}));
}

TEST(UInt128Test, AdditionCarriesAndWraps) {
    EXPECT_EQ((UInt128{0, kMax} + UInt128{0, kMax}), (UInt128{1, kMax - 1}));
    EXPECT_EQ((UInt128{kMax, kMax} + UInt128{0, 1}), (UInt128{0, 0}));  // wraps mod 2^128
}

TEST(UInt128Test, OrderingComparesHighWordFirst) {
    const UInt128 a{1, 0};
    const UInt128 b{0, kMax};
    EXPECT_TRUE(b < a);
    EXPECT_TRUE(a > b);
    EXPECT_TRUE(b <= a);
    EXPECT_TRUE(a >= b);
    EXPECT_TRUE(a <= a);
    EXPECT_TRUE(a != b);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE((UInt128{2, 3}) < (UInt128{2, 4}));
}

#if defined(__SIZEOF_INT128__)
TEST(UInt128Test, MatchesNativeArithmeticOnRandomOperands) {
    __extension__ using Native = unsigned __int128;
    Rng rng(128);
    for (int i = 0; i < 20000; ++i) {
        const std::uint64_t a = rng.nextU64() >> rng.uniformBelow(64);
        const std::uint64_t b = rng.nextU64() >> rng.uniformBelow(64);
        const std::uint64_t c = rng.nextU64();
        const std::uint64_t d = rng.nextU64();
        const Native expected = static_cast<Native>(a) * b + static_cast<Native>(c) * d;
        const UInt128 got = UInt128::multiply(a, b) + UInt128::multiply(c, d);
        ASSERT_EQ(got.high, static_cast<std::uint64_t>(expected >> 64U));
        ASSERT_EQ(got.low, static_cast<std::uint64_t>(expected));
        const Native other = static_cast<Native>(c) * a;
        const UInt128 other_got = UInt128::multiply(c, a);
        ASSERT_EQ(got < other_got, expected < other);
    }
}
#endif

}  // namespace
}  // namespace mapc
