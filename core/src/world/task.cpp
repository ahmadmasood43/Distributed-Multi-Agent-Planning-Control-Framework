#include "mapc/world/task.hpp"

#include "mapc/world/capabilities.hpp"

#include <stdexcept>
#include <string>

namespace mapc {

namespace {

std::uint64_t bitOf(std::size_t index) {
    if (index >= CapabilitySet::kMaxCapabilities) {
        throw std::out_of_range("capability index " + std::to_string(index) +
                                " is out of range (at most " +
                                std::to_string(CapabilitySet::kMaxCapabilities) + " capabilities)");
    }
    return std::uint64_t{1} << index;
}

}  // namespace

CapabilitySet CapabilitySet::of(std::initializer_list<std::size_t> indices) {
    CapabilitySet set;
    for (const std::size_t index : indices) {
        set.add(index);
    }
    return set;
}

CapabilitySet& CapabilitySet::add(std::size_t index) {
    bits_ |= bitOf(index);
    return *this;
}

bool CapabilitySet::contains(std::size_t index) const {
    return (bits_ & bitOf(index)) != 0;
}

std::string_view toString(TaskStatus status) noexcept {
    switch (status) {
        case TaskStatus::Pending:
            return "pending";
        case TaskStatus::Assigned:
            return "assigned";
        case TaskStatus::InProgress:
            return "in progress";
        case TaskStatus::Done:
            return "done";
        case TaskStatus::Failed:
            return "failed";
        case TaskStatus::Cancelled:
            return "cancelled";
    }
    return "unknown status";
}

}  // namespace mapc
