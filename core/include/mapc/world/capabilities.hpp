#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace mapc {

/// A set of agent capabilities (lifting, cold storage, flying, ...), stored
/// as a 64-bit mask. Capabilities are small integer indices in
/// [0, CapabilitySet::kMaxCapabilities); a scenario maps its capability
/// names to indices.
///
/// An agent can do a task when its capabilities contain all of the task's
/// required capabilities.
class CapabilitySet {
public:
    static constexpr std::size_t kMaxCapabilities = 64;

    /// The empty set.
    constexpr CapabilitySet() noexcept = default;

    /// The set of the given indices. Throws std::out_of_range for an index
    /// >= kMaxCapabilities.
    [[nodiscard]] static CapabilitySet of(std::initializer_list<std::size_t> indices);

    /// The set whose members are the 1 bits of `bits` (bit i = capability i).
    [[nodiscard]] static constexpr CapabilitySet fromBits(std::uint64_t bits) noexcept {
        return CapabilitySet{bits};
    }

    /// Adds capability `index`. Throws std::out_of_range for an index
    /// >= kMaxCapabilities.
    CapabilitySet& add(std::size_t index);

    /// Throws std::out_of_range for an index >= kMaxCapabilities.
    [[nodiscard]] bool contains(std::size_t index) const;

    /// True if every member of `other` is a member of this set.
    [[nodiscard]] constexpr bool containsAll(CapabilitySet other) const noexcept {
        return (other.bits_ & ~bits_) == 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

    friend constexpr bool operator==(CapabilitySet a, CapabilitySet b) noexcept {
        return a.bits_ == b.bits_;
    }
    friend constexpr bool operator!=(CapabilitySet a, CapabilitySet b) noexcept {
        return a.bits_ != b.bits_;
    }

private:
    constexpr explicit CapabilitySet(std::uint64_t bits) noexcept : bits_(bits) {}

    std::uint64_t bits_ = 0;
};

}  // namespace mapc
