#include "mapc/common/rng.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

namespace mapc {
namespace {

constexpr auto kI64Max = std::numeric_limits<std::int64_t>::max();
constexpr auto kI64Min = std::numeric_limits<std::int64_t>::min();

// --- Golden values -----------------------------------------------------------
// Reference outputs from an independent implementation of the published
// algorithms. They pin the exact sequence so results are reproducible across
// compilers and standard libraries; a change here breaks every recorded trace.

TEST(SplitMix64Test, MatchesPublishedReferenceSequence) {
    SplitMix64 zero(0);
    EXPECT_EQ(zero.next(), 0xE220A8397B1DCDAFULL);
    EXPECT_EQ(zero.next(), 0x6E789E6AA1B965F4ULL);
    EXPECT_EQ(zero.next(), 0x06C45D188009454FULL);

    SplitMix64 g(1234567);
    const std::array<std::uint64_t, 5> expected{6457827717110365317ULL, 3203168211198807973ULL,
                                                9817491932198370423ULL, 4593380528125082431ULL,
                                                16408922859458223821ULL};
    for (const auto value : expected) {
        EXPECT_EQ(g.next(), value);
    }
}

TEST(RngTest, MatchesGoldenSequence) {
    Rng rng(42);
    EXPECT_EQ(rng.nextU64(), 0x15780B2E0C2EC716ULL);
    EXPECT_EQ(rng.nextU64(), 0x6104D9866D113A7EULL);
    EXPECT_EQ(rng.nextU64(), 0xAE17533239E499A1ULL);
    EXPECT_EQ(rng.nextU64(), 0xECB8AD4703B360A1ULL);
    EXPECT_EQ(rng(), 0xFDE6DC7FE2EC5E64ULL);  // operator() is the same stream
}

TEST(RngTest, SeedZeroIsUsable) {
    Rng rng(0);
    EXPECT_EQ(rng.nextU64(), 0x99EC5F36CB75F2B4ULL);
    EXPECT_EQ(rng.nextU64(), 0xBF6E1F784956452AULL);
    EXPECT_EQ(rng.nextU64(), 0x1A5F849D4933E6E0ULL);
}

TEST(RngTest, DistributionsMatchGoldenValues) {
    Rng below(7);
    std::vector<std::uint64_t> got_below;
    got_below.reserve(10);
    for (int i = 0; i < 10; ++i) {
        got_below.push_back(below.uniformBelow(10));
    }
    EXPECT_EQ(got_below, (std::vector<std::uint64_t>{7, 2, 8, 9, 9, 8, 0, 1, 4, 1}));

    Rng ints(7);
    std::vector<std::int64_t> got_ints;
    got_ints.reserve(10);
    for (int i = 0; i < 10; ++i) {
        got_ints.push_back(ints.uniformInt(-5, 5));
    }
    EXPECT_EQ(got_ints, (std::vector<std::int64_t>{2, -2, 4, 5, 5, 4, -5, -4, -1, -4}));

    // Exact comparisons (not EXPECT_DOUBLE_EQ, which allows 4 ULP): the
    // guarantee is bit-for-bit reproducibility.
    Rng reals(99);
    EXPECT_EQ(reals.uniformReal(), 0x1.65129fad5723ap-2);
    EXPECT_EQ(reals.uniformReal(), 0x1.20c49c7a63b10p-1);
    EXPECT_EQ(reals.uniformReal(), 0x1.834aad80a9084p-2);
}

TEST(RngTest, RealRangeMatchesCorrectlyRoundedFusedReference) {
    // Reference values are lo + width * u rounded once (exact rational
    // arithmetic). The third and fifth draws differ in the last bit from the
    // twice-rounded `lo + width * u`, so they catch accidental FP contraction
    // or a rewrite that drops the explicit fma.
    Rng rng(42);
    EXPECT_EQ(rng.uniformReal(0.1, 0.9), 0x1.56337ac04df80p-3);
    EXPECT_EQ(rng.uniformReal(0.1, 0.9), 0x1.9cdc51ae29d0cp-2);
    EXPECT_EQ(rng.uniformReal(0.1, 0.9), 0x1.49beeb838fd43p-1);
    EXPECT_EQ(rng.uniformReal(0.1, 0.9), 0x1.adf448719f857p-1);
    EXPECT_EQ(rng.uniformReal(0.1, 0.9), 0x1.c97160cc9e470p-1);
}

TEST(RngTest, DeriveMatchesGoldenValue) {
    const Rng root(42);
    Rng child = root.derive(3);
    EXPECT_EQ(child.seed(), 0x964ABE8417436539ULL);
    EXPECT_EQ(child.nextU64(), 0x0ECA52F6C7368C45ULL);
}

// --- Determinism ------------------------------------------------------------

TEST(RngTest, SameSeedSameSequence) {
    Rng a(123);
    Rng b(123);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ(a.nextU64(), b.nextU64()) << "diverged at draw " << i;
    }
}

TEST(RngTest, DifferentSeedsDiverge) {
    Rng a(1);
    Rng b(2);
    int equal = 0;
    for (int i = 0; i < 100; ++i) {
        equal += a.nextU64() == b.nextU64() ? 1 : 0;
    }
    EXPECT_EQ(equal, 0);
}

TEST(RngTest, CopiesContinueIdentically) {
    Rng a(5);
    (void)a.nextU64();
    Rng b = a;
    EXPECT_EQ(a.nextU64(), b.nextU64());
    EXPECT_EQ(a.seed(), b.seed());
}

TEST(RngTest, DeriveIsIndependentOfDrawHistory) {
    const Rng fresh(77);
    Rng used(77);
    for (int i = 0; i < 50; ++i) {
        (void)used.nextU64();
    }
    Rng from_fresh = fresh.derive(9);
    Rng from_used = used.derive(9);
    EXPECT_EQ(from_fresh.nextU64(), from_used.nextU64());
}

TEST(RngTest, DeriveDoesNotAdvanceParent) {
    Rng a(77);
    Rng b(77);
    (void)a.derive(1);
    (void)a.derive(2);
    EXPECT_EQ(a.nextU64(), b.nextU64());
}

TEST(RngTest, DerivedStreamsAreDistinct) {
    const Rng root(2024);
    std::set<std::uint64_t> first_draws;
    std::set<std::uint64_t> seeds;
    for (std::uint64_t key = 0; key < 1000; ++key) {
        Rng child = root.derive(key);
        seeds.insert(child.seed());
        first_draws.insert(child.nextU64());
    }
    EXPECT_EQ(seeds.size(), 1000U);
    EXPECT_EQ(first_draws.size(), 1000U);

    // A derived stream must not alias the root or a root seeded with the key.
    Rng child0 = root.derive(0);
    Rng plain0(0);
    Rng root_copy = root;
    const auto c = child0.nextU64();
    EXPECT_NE(c, plain0.nextU64());
    EXPECT_NE(c, root_copy.nextU64());
}

// --- uniformBelow ------------------------------------------------------------

TEST(RngTest, UniformBelowRejectsZeroBound) {
    Rng rng(1);
    EXPECT_THROW((void)rng.uniformBelow(0), std::invalid_argument);
}

TEST(RngTest, UniformBelowOneIsAlwaysZero) {
    Rng rng(1);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(rng.uniformBelow(1), 0U);
    }
}

TEST(RngTest, UniformBelowStaysInRangeForExtremeBounds) {
    Rng rng(3);
    const std::uint64_t bounds[] = {2,
                                    3,
                                    (1ULL << 32U) - 1,
                                    1ULL << 32U,
                                    (1ULL << 63U) + 1,
                                    std::numeric_limits<std::uint64_t>::max()};
    for (const auto bound : bounds) {
        for (int i = 0; i < 2000; ++i) {
            ASSERT_LT(rng.uniformBelow(bound), bound) << "bound " << bound;
        }
    }
}

TEST(RngTest, UniformBelowHighBoundCoversUpperHalf) {
    // With bound = 2^63 + 1 the results lie in [0, 2^63], so about half of the
    // draws must be >= 2^62. A broken 128-bit multiply would typically collapse
    // the high word and skew this badly.
    Rng rng(11);
    const std::uint64_t bound = (1ULL << 63U) + 1;
    const std::uint64_t midpoint = 1ULL << 62U;
    int upper = 0;
    constexpr int kDraws = 10000;
    for (int i = 0; i < kDraws; ++i) {
        upper += rng.uniformBelow(bound) >= midpoint ? 1 : 0;
    }
    EXPECT_GT(upper, kDraws * 45 / 100);
    EXPECT_LT(upper, kDraws * 55 / 100);
}

TEST(RngTest, UniformBelowMaxBoundUsesFullWidth) {
    // bound = 2^64 - 1: the top bit must be set in about half the draws.
    Rng rng(12);
    const std::uint64_t bound = std::numeric_limits<std::uint64_t>::max();
    int top_bit = 0;
    constexpr int kDraws = 10000;
    for (int i = 0; i < kDraws; ++i) {
        top_bit += rng.uniformBelow(bound) >= (1ULL << 63U) ? 1 : 0;
    }
    EXPECT_GT(top_bit, kDraws * 45 / 100);
    EXPECT_LT(top_bit, kDraws * 55 / 100);
}

TEST(RngTest, UniformBelowIsApproximatelyUniform) {
    // Chi-squared goodness of fit over 10 buckets. With 9 degrees of freedom
    // the 99.99th percentile is ~33.7; the seed is fixed, so this is
    // deterministic, but the bound documents the expected quality.
    Rng rng(2026);
    constexpr std::size_t kBuckets = 10;
    constexpr int kDraws = 100000;
    std::array<int, kBuckets> counts{};
    for (int i = 0; i < kDraws; ++i) {
        ++counts[rng.uniformBelow(kBuckets)];
    }
    const double expected = static_cast<double>(kDraws) / kBuckets;
    double chi2 = 0.0;
    for (const int c : counts) {
        const double diff = static_cast<double>(c) - expected;
        chi2 += diff * diff / expected;
    }
    EXPECT_LT(chi2, 33.7);
}

// --- uniformInt ---------------------------------------------------------------

TEST(RngTest, UniformIntRejectsInvertedRange) {
    Rng rng(1);
    EXPECT_THROW((void)rng.uniformInt(5, 4), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformInt(kI64Max, kI64Min), std::invalid_argument);
}

TEST(RngTest, UniformIntSingletonRange) {
    Rng rng(1);
    EXPECT_EQ(rng.uniformInt(9, 9), 9);
    EXPECT_EQ(rng.uniformInt(kI64Min, kI64Min), kI64Min);
    EXPECT_EQ(rng.uniformInt(kI64Max, kI64Max), kI64Max);
}

TEST(RngTest, UniformIntHitsBothInclusiveEndpoints) {
    Rng rng(8);
    bool saw_lo = false;
    bool saw_hi = false;
    for (int i = 0; i < 1000; ++i) {
        const auto v = rng.uniformInt(-2, 2);
        ASSERT_GE(v, -2);
        ASSERT_LE(v, 2);
        saw_lo = saw_lo || v == -2;
        saw_hi = saw_hi || v == 2;
    }
    EXPECT_TRUE(saw_lo);
    EXPECT_TRUE(saw_hi);
}

TEST(RngTest, UniformIntFullRangeProducesBothSigns) {
    Rng rng(4);
    int negative = 0;
    constexpr int kDraws = 1000;
    for (int i = 0; i < kDraws; ++i) {
        negative += rng.uniformInt(kI64Min, kI64Max) < 0 ? 1 : 0;
    }
    EXPECT_GT(negative, kDraws * 40 / 100);
    EXPECT_LT(negative, kDraws * 60 / 100);
}

TEST(RngTest, UniformIntRangesTouchingTheLimits) {
    Rng rng(6);
    for (int i = 0; i < 1000; ++i) {
        const auto top = rng.uniformInt(kI64Max - 3, kI64Max);
        ASSERT_GE(top, kI64Max - 3);
        const auto bottom = rng.uniformInt(kI64Min, kI64Min + 3);
        ASSERT_LE(bottom, kI64Min + 3);
        const auto wide = rng.uniformInt(kI64Min + 1, kI64Max);  // span of 2^64 - 2
        ASSERT_GT(wide, kI64Min);
        const auto spans_zero = rng.uniformInt(-1, kI64Max);  // span of 2^63
        ASSERT_GE(spans_zero, -1);
    }
}

// --- uniformReal -------------------------------------------------------------

TEST(RngTest, UniformRealUnitIntervalIsHalfOpen) {
    Rng rng(10);
    double lo = 1.0;
    double hi = 0.0;
    for (int i = 0; i < 100000; ++i) {
        const double v = rng.uniformReal();
        ASSERT_GE(v, 0.0);
        ASSERT_LT(v, 1.0);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    EXPECT_LT(lo, 0.001);
    EXPECT_GT(hi, 0.999);
}

TEST(RngTest, UniformRealRangeValidation) {
    Rng rng(1);
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double big = std::numeric_limits<double>::max();
    EXPECT_THROW((void)rng.uniformReal(1.0, 0.0), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformReal(0.0, inf), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformReal(-inf, 0.0), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformReal(nan, 1.0), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformReal(0.0, nan), std::invalid_argument);
    EXPECT_THROW((void)rng.uniformReal(-big, big), std::invalid_argument);  // width overflows
}

TEST(RngTest, UniformRealDegenerateRangeReturnsBound) {
    Rng rng(1);
    EXPECT_EQ(rng.uniformReal(2.5, 2.5), 2.5);
    EXPECT_EQ(rng.uniformReal(-0.0, 0.0), -0.0);
}

TEST(RngTest, UniformRealNeverReturnsUpperBound) {
    // [1, nextafter(1)) contains exactly one double; rounding lo + width * u
    // would otherwise produce hi for u close to 1.
    Rng rng(12);
    const double lo = 1.0;
    const double hi = std::nextafter(1.0, 2.0);
    for (int i = 0; i < 10000; ++i) {
        ASSERT_EQ(rng.uniformReal(lo, hi), lo);
    }
    // Large magnitudes where the spacing between doubles exceeds 1.
    const double big_lo = 1e300;
    const double big_hi = 1.0000000001e300;
    for (int i = 0; i < 10000; ++i) {
        const double v = rng.uniformReal(big_lo, big_hi);
        ASSERT_GE(v, big_lo);
        ASSERT_LT(v, big_hi);
    }
}

TEST(RngTest, UniformRealNegativeRange) {
    Rng rng(13);
    for (int i = 0; i < 1000; ++i) {
        const double v = rng.uniformReal(-3.0, -1.0);
        ASSERT_GE(v, -3.0);
        ASSERT_LT(v, -1.0);
    }
}

// --- bernoulli -----------------------------------------------------------------

TEST(RngTest, BernoulliExtremes) {
    Rng rng(14);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_FALSE(rng.bernoulli(0.0));
        ASSERT_FALSE(rng.bernoulli(-1.0));
        ASSERT_FALSE(rng.bernoulli(-std::numeric_limits<double>::infinity()));
        ASSERT_TRUE(rng.bernoulli(1.0));
        ASSERT_TRUE(rng.bernoulli(2.0));
        ASSERT_TRUE(rng.bernoulli(std::numeric_limits<double>::infinity()));
    }
}

TEST(RngTest, BernoulliRejectsNaN) {
    Rng rng(14);
    EXPECT_THROW((void)rng.bernoulli(std::numeric_limits<double>::quiet_NaN()),
                 std::invalid_argument);
}

TEST(RngTest, BernoulliExtremesDoNotConsumeRandomness) {
    // Degenerate probabilities short-circuit, so they must not shift the stream.
    Rng a(15);
    Rng b(15);
    (void)a.bernoulli(0.0);
    (void)a.bernoulli(1.0);
    EXPECT_EQ(a.nextU64(), b.nextU64());
}

TEST(RngTest, BernoulliFrequencyMatchesProbability) {
    Rng rng(16);
    constexpr int kDraws = 100000;
    int hits = 0;
    for (int i = 0; i < kDraws; ++i) {
        hits += rng.bernoulli(0.3) ? 1 : 0;
    }
    // Standard deviation is ~145; allow > 6 sigma.
    EXPECT_NEAR(hits, 30000, 900);
}

// --- shuffle -----------------------------------------------------------------

TEST(RngTest, ShuffleOfEmptyAndSingletonIsNoOp) {
    Rng rng(17);
    std::vector<int> empty;
    rng.shuffle(empty.begin(), empty.end());
    EXPECT_TRUE(empty.empty());

    std::vector<int> one{42};
    rng.shuffle(one.begin(), one.end());
    EXPECT_EQ(one, std::vector<int>{42});
}

TEST(RngTest, ShuffleIsAPermutation) {
    Rng rng(18);
    std::vector<int> v(100);
    std::iota(v.begin(), v.end(), 0);
    auto shuffled = v;
    rng.shuffle(shuffled.begin(), shuffled.end());
    EXPECT_NE(shuffled, v);
    EXPECT_TRUE(std::is_permutation(shuffled.begin(), shuffled.end(), v.begin()));
}

TEST(RngTest, ShuffleIsDeterministicForSeed) {
    std::vector<int> a(20);
    std::iota(a.begin(), a.end(), 0);
    auto b = a;
    Rng ra(19);
    Rng rb(19);
    ra.shuffle(a.begin(), a.end());
    rb.shuffle(b.begin(), b.end());
    EXPECT_EQ(a, b);
}

TEST(RngTest, ShuffleWorksOnRawArrays) {
    Rng rng(20);
    int values[] = {1, 2, 3, 4, 5};
    rng.shuffle(std::begin(values), std::end(values));
    std::sort(std::begin(values), std::end(values));
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[4], 5);
}

TEST(RngTest, ShuffleProducesAllPermutationsUniformly) {
    // 3 elements -> 6 permutations; each should appear ~1/6 of the time.
    Rng rng(21);
    std::array<int, 6> counts{};
    constexpr int kTrials = 60000;
    for (int t = 0; t < kTrials; ++t) {
        std::array<int, 3> p{0, 1, 2};
        rng.shuffle(p.begin(), p.end());
        ++counts[(static_cast<std::size_t>(p[0]) * 2U) + (p[1] < p[2] ? 0U : 1U)];
    }
    for (const int c : counts) {
        EXPECT_NEAR(c, kTrials / 6.0, 600.0);
    }
}

// --- Standard library interoperability ----------------------------------------

TEST(RngTest, SatisfiesUniformRandomBitGenerator) {
    static_assert(Rng::min() == 0);
    static_assert(Rng::max() == std::numeric_limits<std::uint64_t>::max());
    Rng rng(22);
    std::uniform_int_distribution<int> dist(1, 6);
    const int roll = dist(rng);
    EXPECT_GE(roll, 1);
    EXPECT_LE(roll, 6);
}

}  // namespace
}  // namespace mapc
