#include "mapc/common/time.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace mapc {
namespace {

constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
constexpr auto kMin = std::numeric_limits<std::int64_t>::min();

// --- Compile-time guarantees -----------------------------------------------

template <typename A, typename B, typename = void>
struct CanAdd : std::false_type {};
template <typename A, typename B>
struct CanAdd<A, B, std::void_t<decltype(std::declval<A>() + std::declval<B>())>> : std::true_type {
};

static_assert(CanAdd<Tick, Duration>::value);
static_assert(CanAdd<Duration, Tick>::value);
static_assert(CanAdd<Duration, Duration>::value);
static_assert(!CanAdd<Tick, Tick>::value, "adding two points in time is meaningless");
static_assert(!CanAdd<Tick, std::int64_t>::value, "raw integers must be wrapped in Duration");
static_assert(!std::is_convertible_v<std::int64_t, Tick>);
static_assert(!std::is_convertible_v<std::int64_t, Duration>);
static_assert(!std::is_convertible_v<Tick, Duration>);
static_assert(std::is_same_v<decltype(Tick{} - Tick{}), Duration>);
static_assert(std::is_trivially_copyable_v<Tick> && sizeof(Tick) == sizeof(std::int64_t));

static_assert(Tick{3} + Duration{4} == Tick{7});
static_assert(Tick{10} - Tick{4} == Duration{6});
static_assert(Tick::max() + Duration{1} == Tick::max());
static_assert(Duration{2} * 3 == Duration{6});

// --- Saturating helpers (exhaustive edge cases) -----------------------------

TEST(SaturatingArithmeticTest, AddClampsAtBothEnds) {
    EXPECT_EQ(detail::saturatingAdd(kMax, 1), kMax);
    EXPECT_EQ(detail::saturatingAdd(kMax, kMax), kMax);
    EXPECT_EQ(detail::saturatingAdd(kMin, -1), kMin);
    EXPECT_EQ(detail::saturatingAdd(kMin, kMin), kMin);
    EXPECT_EQ(detail::saturatingAdd(kMax, kMin), -1);
    EXPECT_EQ(detail::saturatingAdd(kMax - 1, 1), kMax);
    EXPECT_EQ(detail::saturatingAdd(-5, 3), -2);
}

TEST(SaturatingArithmeticTest, SubClampsAtBothEnds) {
    EXPECT_EQ(detail::saturatingSub(kMin, 1), kMin);
    EXPECT_EQ(detail::saturatingSub(kMax, -1), kMax);
    EXPECT_EQ(detail::saturatingSub(0, kMin), kMax);  // -kMin is not representable
    EXPECT_EQ(detail::saturatingSub(-1, kMin), kMax);
    EXPECT_EQ(detail::saturatingSub(-2, kMin), kMax - 1);
    EXPECT_EQ(detail::saturatingSub(kMax, kMax), 0);
    EXPECT_EQ(detail::saturatingSub(kMin, kMin), 0);
    EXPECT_EQ(detail::saturatingSub(kMin, kMax), kMin);
    EXPECT_EQ(detail::saturatingSub(kMax, kMin), kMax);
}

TEST(SaturatingArithmeticTest, MulClampsWithCorrectSign) {
    EXPECT_EQ(detail::saturatingMul(0, kMin), 0);
    EXPECT_EQ(detail::saturatingMul(kMax, 0), 0);
    EXPECT_EQ(detail::saturatingMul(kMax, 2), kMax);
    EXPECT_EQ(detail::saturatingMul(kMax, -2), kMin);
    EXPECT_EQ(detail::saturatingMul(-2, kMax), kMin);
    EXPECT_EQ(detail::saturatingMul(kMin, 2), kMin);
    EXPECT_EQ(detail::saturatingMul(kMin, -1), kMax);  // the classic overflow
    EXPECT_EQ(detail::saturatingMul(-1, kMin), kMax);
    EXPECT_EQ(detail::saturatingMul(kMin, 1), kMin);
    EXPECT_EQ(detail::saturatingMul(-1, kMax), -kMax);
    EXPECT_EQ(detail::saturatingMul(-3, -4), 12);
    EXPECT_EQ(detail::saturatingMul(kMin / 2, 2), kMin);  // exact, no clamping
    EXPECT_EQ(detail::saturatingMul(kMax / 2, 2), kMax - 1);
    EXPECT_EQ(detail::saturatingMul(3037000500LL, 3037000500LL), kMax);  // just past sqrt(max)
    EXPECT_EQ(detail::saturatingMul(3037000499LL, 3037000499LL), 9223372030926249001LL);
}

#if defined(__SIZEOF_INT128__)
TEST(SaturatingArithmeticTest, MulMatchesExactReferenceOnSampledGrid) {
    // Exact 128-bit reference; floating point cannot distinguish values near
    // INT64_MAX on targets where long double has only 53 bits.
    __extension__ using Wide = __int128;
    const std::int64_t samples[] = {
        kMin, kMin + 1, -4294967296LL, -3037000500LL, -3037000499LL, -65536,       -2,       -1,  0,
        1,    2,        65536,         3037000499LL,  3037000500LL,  4294967296LL, kMax - 1, kMax};
    for (const auto a : samples) {
        for (const auto b : samples) {
            const Wide product = static_cast<Wide>(a) * static_cast<Wide>(b);
            const Wide clamped = std::clamp(product, Wide{kMin}, Wide{kMax});
            EXPECT_EQ(detail::saturatingMul(a, b), static_cast<std::int64_t>(clamped))
                << a << " * " << b;
        }
    }
}
#endif

// --- Duration --------------------------------------------------------------

TEST(DurationTest, DefaultIsZero) {
    EXPECT_EQ(Duration{}, Duration::zero());
    EXPECT_EQ(Duration{}.count(), 0);
}

TEST(DurationTest, ArithmeticAndComparison) {
    Duration d{5};
    d += Duration{3};
    EXPECT_EQ(d, Duration{8});
    d -= Duration{10};
    EXPECT_EQ(d, Duration{-2});
    EXPECT_EQ(-d, Duration{2});
    EXPECT_EQ(3 * Duration{4}, Duration{12});
    EXPECT_EQ(Duration{4} * -3, Duration{-12});
    EXPECT_LT(Duration{-1}, Duration::zero());
    EXPECT_GT(Duration::max(), Duration{0});
    EXPECT_LE(Duration::min(), Duration::max());
    EXPECT_NE(Duration{1}, Duration{2});
    EXPECT_GE(Duration{2}, Duration{2});
}

TEST(DurationTest, NegatingMinimumSaturatesToMaximum) {
    EXPECT_EQ(-Duration::min(), Duration::max());
    EXPECT_EQ(-Duration::max(), Duration{-kMax});
}

TEST(DurationTest, SaturationIsNotAbsorbing) {
    // Documented behaviour: max is a clamp, not an infinity.
    EXPECT_EQ(Duration::max() + Duration{1}, Duration::max());
    EXPECT_EQ(Duration::max() - Duration{1}, Duration{kMax - 1});
}

TEST(DurationTest, StreamsWithUnits) {
    std::ostringstream os;
    os << Duration{1} << ", " << Duration{-1} << ", " << Duration{0} << ", " << Duration{7};
    EXPECT_EQ(os.str(), "1 tick, -1 tick, 0 ticks, 7 ticks");
}

// --- Tick ------------------------------------------------------------------

TEST(TickTest, DefaultIsZero) {
    EXPECT_EQ(Tick{}, Tick::zero());
    EXPECT_EQ(Tick{}.value(), 0);
}

TEST(TickTest, PointDurationArithmetic) {
    Tick t{10};
    t += Duration{5};
    EXPECT_EQ(t, Tick{15});
    t -= Duration{20};
    EXPECT_EQ(t, Tick{-5});
    EXPECT_EQ(Tick{3} - Duration{1}, Tick{2});
    EXPECT_EQ(Duration{1} + Tick{3}, Tick{4});
    EXPECT_EQ(Tick{3} - Tick{10}, Duration{-7});
}

TEST(TickTest, IncrementOperators) {
    Tick t{0};
    EXPECT_EQ(t++, Tick{0});
    EXPECT_EQ(t, Tick{1});
    EXPECT_EQ(++t, Tick{2});
    Tick at_max = Tick::max();
    ++at_max;
    EXPECT_EQ(at_max, Tick::max());
}

TEST(TickTest, SaturatesAtTimelineEnds) {
    EXPECT_EQ(Tick::max() + Duration{1}, Tick::max());
    EXPECT_EQ(Tick::max() + Duration::max(), Tick::max());
    EXPECT_EQ(Tick::min() - Duration{1}, Tick::min());
    EXPECT_EQ(Tick::min() + Duration::min(), Tick::min());
    EXPECT_EQ(Tick::max() - Tick::min(), Duration::max());
    EXPECT_EQ(Tick::min() - Tick::max(), Duration::min());
    EXPECT_EQ(Tick::zero() - Duration::min(), Tick::max());
}

TEST(TickTest, NeverSentinelOrdersAfterAnyFiniteTick) {
    EXPECT_LT(Tick{1'000'000'000}, Tick::max());
    EXPECT_GT(Tick::max(), Tick{1} + Duration{1'000'000});
}

TEST(TickTest, StreamsAndHashes) {
    std::ostringstream os;
    os << Tick{42} << ' ' << Tick{-1};
    EXPECT_EQ(os.str(), "t=42 t=-1");

    const std::unordered_set<Tick> ticks{Tick{1}, Tick{1}, Tick{2}};
    EXPECT_EQ(ticks.size(), 2U);
}

}  // namespace
}  // namespace mapc
