#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>

namespace mapc {

/// A type-safe integer identifier.
///
/// Each `Tag` produces a distinct type, so an `AgentId` cannot be passed where a
/// `NodeId` is expected and neither converts implicitly to or from an integer.
/// A default-constructed id is *invalid*; the largest representable value is
/// reserved as the invalid sentinel so that 0 remains a usable id.
///
/// `Tag` must provide `static constexpr const char* kName`, used when printing.
template <typename Tag, typename Rep = std::uint32_t>
class StrongId {
    static_assert(std::is_integral_v<Rep> && std::is_unsigned_v<Rep>,
                  "StrongId requires an unsigned integral representation");
    static_assert(!std::is_same_v<Rep, bool>, "StrongId cannot be backed by bool");

public:
    using tag_type = Tag;
    using rep_type = Rep;

    static constexpr Rep kInvalidValue = std::numeric_limits<Rep>::max();

    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(Rep value) noexcept : value_(value) {}

    [[nodiscard]] static constexpr StrongId invalid() noexcept { return StrongId{}; }

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != kInvalidValue; }

    friend constexpr bool operator==(StrongId a, StrongId b) noexcept {
        return a.value_ == b.value_;
    }
    friend constexpr bool operator!=(StrongId a, StrongId b) noexcept {
        return a.value_ != b.value_;
    }
    friend constexpr bool operator<(StrongId a, StrongId b) noexcept { return a.value_ < b.value_; }
    friend constexpr bool operator<=(StrongId a, StrongId b) noexcept {
        return a.value_ <= b.value_;
    }
    friend constexpr bool operator>(StrongId a, StrongId b) noexcept { return a.value_ > b.value_; }
    friend constexpr bool operator>=(StrongId a, StrongId b) noexcept {
        return a.value_ >= b.value_;
    }

    /// Prints `<kName>#<value>`, or `<kName>#invalid` for the sentinel.
    friend std::ostream& operator<<(std::ostream& os, StrongId id) {
        os << Tag::kName << '#';
        if (id.valid()) {
            // Unary + promotes 8-bit reps so they print as numbers, not chars.
            os << +id.value_;
        } else {
            os << "invalid";
        }
        return os;
    }

private:
    Rep value_ = kInvalidValue;
};

template <typename Tag, typename Rep>
[[nodiscard]] std::string toString(StrongId<Tag, Rep> id) {
    std::string out = Tag::kName;
    out += '#';
    out += id.valid() ? std::to_string(+id.value()) : std::string("invalid");
    return out;
}

// clang-format off
struct AgentIdTag    { static constexpr const char* kName = "agent"; };
struct TaskIdTag     { static constexpr const char* kName = "task"; };
struct NodeIdTag     { static constexpr const char* kName = "node"; };
struct EdgeIdTag     { static constexpr const char* kName = "edge"; };
struct ResourceIdTag { static constexpr const char* kName = "resource"; };
// clang-format on

using AgentId = StrongId<AgentIdTag>;
using TaskId = StrongId<TaskIdTag>;
using NodeId = StrongId<NodeIdTag>;
using EdgeId = StrongId<EdgeIdTag>;
using ResourceId = StrongId<ResourceIdTag>;

}  // namespace mapc

// Specialising std::hash for a user-defined type is explicitly permitted.
template <typename Tag, typename Rep>
struct std::hash<mapc::StrongId<Tag, Rep>> {  // NOLINT(cert-dcl58-cpp)
    std::size_t operator()(mapc::StrongId<Tag, Rep> id) const noexcept {
        return std::hash<Rep>{}(id.value());
    }
};
