#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace mapc {

/// SplitMix64 (Steele, Lea, Flood 2014). Used for seeding and for deriving
/// independent seeds; not intended as the main simulation generator.
class SplitMix64 {
public:
    constexpr explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    constexpr std::uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        return mix(state_);
    }

    /// The SplitMix64 output finaliser: a bijective 64-bit mixing function.
    [[nodiscard]] static constexpr std::uint64_t mix(std::uint64_t z) noexcept {
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

private:
    std::uint64_t state_;
};

/// Deterministic pseudo-random number generator for simulations and tests.
///
/// The engine is xoshiro256** (Blackman & Vigna), seeded through SplitMix64.
/// All distributions are implemented here rather than taken from `<random>`,
/// because the standard distributions are not specified bit-for-bit and differ
/// between libstdc++, libc++ and MSVC. The same seed therefore produces the
/// same sequence on every platform, which the trace-determinism guarantees
/// depend on. Floating-point results are bit-exact too: they use only exactly
/// representable scalings and explicit fused multiply-adds, so they do not
/// depend on the compiler's floating-point contraction settings.
///
/// `Rng` satisfies UniformRandomBitGenerator, but prefer the member
/// distributions (and `shuffle`) over `<random>` ones to keep that guarantee.
///
/// Not thread-safe: give each thread or agent its own instance via `derive()`.
class Rng {
public:
    using result_type = std::uint64_t;

    explicit Rng(std::uint64_t seed) noexcept;

    [[nodiscard]] static constexpr result_type min() noexcept { return 0; }
    [[nodiscard]] static constexpr result_type max() noexcept {
        return std::numeric_limits<result_type>::max();
    }

    /// The seed this generator was constructed from.
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

    /// Next raw 64-bit output.
    std::uint64_t nextU64() noexcept;
    result_type operator()() noexcept { return nextU64(); }

    /// Uniform integer in [0, bound). Unbiased (Lemire's method).
    /// Throws std::invalid_argument if bound == 0.
    std::uint64_t uniformBelow(std::uint64_t bound);

    /// Uniform integer in [lo, hi], inclusive at both ends. Unbiased; the full
    /// int64 range is supported. Throws std::invalid_argument if lo > hi.
    std::int64_t uniformInt(std::int64_t lo, std::int64_t hi);

    /// Uniform double in [0, 1) with 53 bits of randomness.
    double uniformReal() noexcept;

    /// Uniform double in [lo, hi). Returns lo when lo == hi. Throws
    /// std::invalid_argument if lo > hi or either bound is not finite, or if
    /// hi - lo overflows to infinity.
    double uniformReal(double lo, double hi);

    /// True with probability p. p <= 0 is always false and p >= 1 always true.
    /// Throws std::invalid_argument if p is NaN.
    bool bernoulli(double p);

    /// Uniformly shuffles [first, last) with Fisher-Yates.
    template <typename RandomIt>
    void shuffle(RandomIt first, RandomIt last) {
        using Diff = typename std::iterator_traits<RandomIt>::difference_type;
        static_assert(std::is_base_of_v<std::random_access_iterator_tag,
                                        typename std::iterator_traits<RandomIt>::iterator_category>,
                      "Rng::shuffle requires random-access iterators");
        const Diff n = last - first;
        for (Diff i = n - 1; i > 0; --i) {
            const auto j = static_cast<Diff>(uniformBelow(static_cast<std::uint64_t>(i) + 1U));
            using std::swap;
            swap(first[i], first[j]);
        }
    }

    /// Returns a generator for an independent stream identified by `key`.
    ///
    /// The result depends only on this generator's seed and `key`, never on how
    /// many numbers have been drawn, so `root.derive(agentId)` yields the same
    /// stream regardless of creation order or thread scheduling.
    [[nodiscard]] Rng derive(std::uint64_t key) const noexcept;

private:
    std::uint64_t seed_;
    std::array<std::uint64_t, 4> state_{};
};

}  // namespace mapc
