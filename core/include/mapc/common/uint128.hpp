#pragma once

#include <cstdint>

namespace mapc {

/// An unsigned 128-bit integer with just the operations exact integer
/// arithmetic in this project needs (full 64x64 products, addition,
/// comparison). Portable: no reliance on unsigned __int128, which -Wpedantic
/// rejects and some compilers lack. Addition wraps modulo 2^128.
struct UInt128 {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    /// The exact product a * b.
    [[nodiscard]] static constexpr UInt128 multiply(std::uint64_t a, std::uint64_t b) noexcept {
        constexpr std::uint64_t kLowMask = 0xFFFFFFFFULL;
        const std::uint64_t a_lo = a & kLowMask;
        const std::uint64_t a_hi = a >> 32U;
        const std::uint64_t b_lo = b & kLowMask;
        const std::uint64_t b_hi = b >> 32U;

        const std::uint64_t lo_lo = a_lo * b_lo;
        const std::uint64_t hi_lo = a_hi * b_lo;
        const std::uint64_t lo_hi = a_lo * b_hi;
        const std::uint64_t hi_hi = a_hi * b_hi;

        const std::uint64_t cross = (lo_lo >> 32U) + (hi_lo & kLowMask) + lo_hi;
        return UInt128{(hi_lo >> 32U) + (cross >> 32U) + hi_hi,
                       (cross << 32U) | (lo_lo & kLowMask)};
    }

    friend constexpr UInt128 operator+(UInt128 a, UInt128 b) noexcept {
        const std::uint64_t sum_low = a.low + b.low;
        const std::uint64_t carry = sum_low < a.low ? 1U : 0U;
        return UInt128{a.high + b.high + carry, sum_low};
    }

    friend constexpr bool operator==(UInt128 a, UInt128 b) noexcept {
        return a.high == b.high && a.low == b.low;
    }
    friend constexpr bool operator!=(UInt128 a, UInt128 b) noexcept { return !(a == b); }
    friend constexpr bool operator<(UInt128 a, UInt128 b) noexcept {
        return a.high != b.high ? a.high < b.high : a.low < b.low;
    }
    friend constexpr bool operator>(UInt128 a, UInt128 b) noexcept { return b < a; }
    friend constexpr bool operator<=(UInt128 a, UInt128 b) noexcept { return !(b < a); }
    friend constexpr bool operator>=(UInt128 a, UInt128 b) noexcept { return !(a < b); }
};

}  // namespace mapc
