#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>

namespace mapc {

namespace detail {

using TickRep = std::int64_t;

inline constexpr TickRep kTickMax = std::numeric_limits<TickRep>::max();
inline constexpr TickRep kTickMin = std::numeric_limits<TickRep>::min();

/// a + b, clamped to [kTickMin, kTickMax] instead of overflowing.
[[nodiscard]] constexpr TickRep saturatingAdd(TickRep a, TickRep b) noexcept {
    if (b > 0 && a > kTickMax - b) {
        return kTickMax;
    }
    if (b < 0 && a < kTickMin - b) {
        return kTickMin;
    }
    return a + b;
}

/// a - b, clamped to [kTickMin, kTickMax] instead of overflowing.
[[nodiscard]] constexpr TickRep saturatingSub(TickRep a, TickRep b) noexcept {
    if (b < 0 && a > kTickMax + b) {
        return kTickMax;
    }
    if (b > 0 && a < kTickMin + b) {
        return kTickMin;
    }
    return a - b;
}

/// a * b, clamped to [kTickMin, kTickMax] instead of overflowing.
[[nodiscard]] constexpr TickRep saturatingMul(TickRep a, TickRep b) noexcept {
    if (a == 0 || b == 0) {
        return 0;
    }
    const bool negative = (a < 0) != (b < 0);
    if (a > 0) {
        if (b > 0 ? a > kTickMax / b : b < kTickMin / a) {
            return negative ? kTickMin : kTickMax;
        }
    } else {
        if (b > 0 ? a < kTickMin / b : b < kTickMax / a) {
            return negative ? kTickMin : kTickMax;
        }
    }
    return a * b;
}

}  // namespace detail

/// A signed span of simulation time, measured in whole ticks.
///
/// Arithmetic saturates at the representable limits instead of overflowing, so
/// `Duration::max()` can safely be used as an "unbounded" horizon. Saturation is
/// not absorbing: `Duration::max() - Duration{1}` is a finite value again.
class Duration {
public:
    using rep = detail::TickRep;

    constexpr Duration() noexcept = default;
    constexpr explicit Duration(rep ticks) noexcept : ticks_(ticks) {}

    [[nodiscard]] static constexpr Duration zero() noexcept { return Duration{}; }
    [[nodiscard]] static constexpr Duration max() noexcept { return Duration{detail::kTickMax}; }
    [[nodiscard]] static constexpr Duration min() noexcept { return Duration{detail::kTickMin}; }

    [[nodiscard]] constexpr rep count() const noexcept { return ticks_; }

    constexpr Duration operator-() const noexcept {
        return Duration{detail::saturatingSub(0, ticks_)};
    }

    constexpr Duration& operator+=(Duration d) noexcept {
        ticks_ = detail::saturatingAdd(ticks_, d.ticks_);
        return *this;
    }
    constexpr Duration& operator-=(Duration d) noexcept {
        ticks_ = detail::saturatingSub(ticks_, d.ticks_);
        return *this;
    }
    constexpr Duration& operator*=(rep factor) noexcept {
        ticks_ = detail::saturatingMul(ticks_, factor);
        return *this;
    }

    friend constexpr Duration operator+(Duration a, Duration b) noexcept { return a += b; }
    friend constexpr Duration operator-(Duration a, Duration b) noexcept { return a -= b; }
    friend constexpr Duration operator*(Duration d, rep factor) noexcept { return d *= factor; }
    friend constexpr Duration operator*(rep factor, Duration d) noexcept { return d *= factor; }

    friend constexpr bool operator==(Duration a, Duration b) noexcept {
        return a.ticks_ == b.ticks_;
    }
    friend constexpr bool operator!=(Duration a, Duration b) noexcept {
        return a.ticks_ != b.ticks_;
    }
    friend constexpr bool operator<(Duration a, Duration b) noexcept { return a.ticks_ < b.ticks_; }
    friend constexpr bool operator<=(Duration a, Duration b) noexcept {
        return a.ticks_ <= b.ticks_;
    }
    friend constexpr bool operator>(Duration a, Duration b) noexcept { return a.ticks_ > b.ticks_; }
    friend constexpr bool operator>=(Duration a, Duration b) noexcept {
        return a.ticks_ >= b.ticks_;
    }

    friend std::ostream& operator<<(std::ostream& os, Duration d) {
        return os << d.ticks_ << (d.ticks_ == 1 || d.ticks_ == -1 ? " tick" : " ticks");
    }

private:
    rep ticks_ = 0;
};

/// A point on the discrete simulation timeline.
///
/// Ticks and durations are distinct types: two ticks can be subtracted to give a
/// `Duration`, and a `Duration` can be added to a tick, but two ticks cannot be
/// added. Arithmetic saturates like `Duration`. `Tick::max()` is a convenient
/// "never" sentinel for open-ended reservations.
class Tick {
public:
    using rep = detail::TickRep;

    constexpr Tick() noexcept = default;
    constexpr explicit Tick(rep value) noexcept : value_(value) {}

    [[nodiscard]] static constexpr Tick zero() noexcept { return Tick{}; }
    [[nodiscard]] static constexpr Tick max() noexcept { return Tick{detail::kTickMax}; }
    [[nodiscard]] static constexpr Tick min() noexcept { return Tick{detail::kTickMin}; }

    [[nodiscard]] constexpr rep value() const noexcept { return value_; }

    constexpr Tick& operator+=(Duration d) noexcept {
        value_ = detail::saturatingAdd(value_, d.count());
        return *this;
    }
    constexpr Tick& operator-=(Duration d) noexcept {
        value_ = detail::saturatingSub(value_, d.count());
        return *this;
    }
    constexpr Tick& operator++() noexcept { return *this += Duration{1}; }
    constexpr Tick operator++(int) noexcept {
        Tick previous = *this;
        ++*this;
        return previous;
    }

    friend constexpr Tick operator+(Tick t, Duration d) noexcept { return t += d; }
    friend constexpr Tick operator+(Duration d, Tick t) noexcept { return t += d; }
    friend constexpr Tick operator-(Tick t, Duration d) noexcept { return t -= d; }
    friend constexpr Duration operator-(Tick a, Tick b) noexcept {
        return Duration{detail::saturatingSub(a.value_, b.value_)};
    }

    friend constexpr bool operator==(Tick a, Tick b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(Tick a, Tick b) noexcept { return a.value_ != b.value_; }
    friend constexpr bool operator<(Tick a, Tick b) noexcept { return a.value_ < b.value_; }
    friend constexpr bool operator<=(Tick a, Tick b) noexcept { return a.value_ <= b.value_; }
    friend constexpr bool operator>(Tick a, Tick b) noexcept { return a.value_ > b.value_; }
    friend constexpr bool operator>=(Tick a, Tick b) noexcept { return a.value_ >= b.value_; }

    friend std::ostream& operator<<(std::ostream& os, Tick t) { return os << "t=" << t.value_; }

private:
    rep value_ = 0;
};

}  // namespace mapc

template <>
struct std::hash<mapc::Tick> {
    std::size_t operator()(mapc::Tick t) const noexcept {
        return std::hash<mapc::Tick::rep>{}(t.value());
    }
};
