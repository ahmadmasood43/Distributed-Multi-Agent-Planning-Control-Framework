#include "mapc/common/rng.hpp"

#include "mapc/common/uint128.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace mapc {

namespace {

constexpr std::uint64_t rotl(std::uint64_t x, unsigned k) noexcept {
    return (x << k) | (x >> (64U - k));
}

// Domain-separation constant so derived streams are unrelated to Rng(key).
constexpr std::uint64_t kDeriveSalt = 0xD1B54A32D192ED03ULL;

}  // namespace

Rng::Rng(std::uint64_t seed) noexcept : seed_(seed) {
    SplitMix64 seeder(seed);
    for (auto& word : state_) {
        word = seeder.next();
    }
    // SplitMix64 is a bijection on its counter, so four consecutive outputs can
    // never all be zero; the all-zero state (a fixed point of xoshiro) is
    // therefore unreachable.
}

std::uint64_t Rng::nextU64() noexcept {
    const std::uint64_t result = rotl(state_[1] * 5U, 7U) * 9U;
    const std::uint64_t t = state_[1] << 17U;

    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];

    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45U);

    return result;
}

std::uint64_t Rng::uniformBelow(std::uint64_t bound) {
    if (bound == 0) {
        throw std::invalid_argument("Rng::uniformBelow: bound must be positive");
    }
    // Lemire, "Fast Random Integer Generation in an Interval" (2019).
    // The high word of the 128-bit product is the candidate result; the low
    // word decides whether it falls in the biased region that must be redrawn.
    auto product = UInt128::multiply(nextU64(), bound);
    if (product.low < bound) {
        const std::uint64_t threshold = (0U - bound) % bound;
        while (product.low < threshold) {
            product = UInt128::multiply(nextU64(), bound);
        }
    }
    return product.high;
}

std::int64_t Rng::uniformInt(std::int64_t lo, std::int64_t hi) {
    if (lo > hi) {
        throw std::invalid_argument("Rng::uniformInt: lo must not exceed hi");
    }
    // Work in unsigned arithmetic, where wrap-around is well defined.
    const auto ulo = static_cast<std::uint64_t>(lo);
    const std::uint64_t span = static_cast<std::uint64_t>(hi) - ulo;  // hi - lo, exact mod 2^64
    const std::uint64_t offset =
        span == std::numeric_limits<std::uint64_t>::max() ? nextU64() : uniformBelow(span + 1U);
    // Converting back is well defined for GCC/Clang/MSVC (two's complement) and
    // guaranteed by the standard from C++20 on.
    return static_cast<std::int64_t>(ulo + offset);
}

double Rng::uniformReal() noexcept {
    // Top 53 bits scaled by 2^-53: every result is exactly representable and
    // strictly below 1.
    constexpr double kScale = 0x1.0p-53;
    return static_cast<double>(nextU64() >> 11U) * kScale;
}

double Rng::uniformReal(double lo, double hi) {
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
        throw std::invalid_argument("Rng::uniformReal: bounds must be finite");
    }
    if (lo > hi) {
        throw std::invalid_argument("Rng::uniformReal: lo must not exceed hi");
    }
    if (lo == hi) {
        return lo;
    }
    const double width = hi - lo;
    if (!std::isfinite(width)) {
        throw std::invalid_argument("Rng::uniformReal: hi - lo overflows");
    }
    // An explicit fused multiply-add gives one IEEE-754 rounding on every
    // platform. Writing `lo + width * u` would let the compiler choose between
    // one and two roundings (FP contraction), so results would depend on the
    // target ISA and compiler flags.
    const double r = std::fma(width, uniformReal(), lo);
    // Rounding can land exactly on hi; keep the interval half-open.
    return r < hi ? r : std::nextafter(hi, lo);
}

bool Rng::bernoulli(double p) {
    if (std::isnan(p)) {
        throw std::invalid_argument("Rng::bernoulli: p must not be NaN");
    }
    if (p <= 0.0) {
        return false;
    }
    if (p >= 1.0) {
        return true;
    }
    return uniformReal() < p;
}

Rng Rng::derive(std::uint64_t key) const noexcept {
    const std::uint64_t child_seed =
        SplitMix64::mix(SplitMix64::mix(seed_ ^ kDeriveSalt) + SplitMix64::mix(key));
    return Rng(child_seed);
}

}  // namespace mapc
