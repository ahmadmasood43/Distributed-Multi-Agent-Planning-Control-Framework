#pragma once

#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/world/capabilities.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace mapc {

enum class TaskStatus : std::uint8_t {
    Pending,     ///< waiting to be allocated
    Assigned,    ///< allocated to an agent that has not started it
    InProgress,  ///< being carried out
    Done,
    Failed,
    Cancelled,
};

[[nodiscard]] std::string_view toString(TaskStatus status) noexcept;

/// What an agent needs in order to carry out a task. Resource needs (docks,
/// chargers) arrive with the resource manager in M8.
struct Requirements {
    /// The agent must have all of these.
    CapabilitySet capabilities;
};

/// A unit of work: go to `source`, then carry something to `destination`.
struct Task {
    TaskId id;
    NodeId source;
    NodeId destination;
    /// Higher means more urgent.
    std::int32_t priority = 0;
    std::optional<Tick> deadline;
    Requirements requirements;
    TaskStatus status = TaskStatus::Pending;
};

}  // namespace mapc
