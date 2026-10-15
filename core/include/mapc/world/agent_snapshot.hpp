#pragma once

#include "mapc/common/strong_id.hpp"
#include "mapc/world/capabilities.hpp"

namespace mapc {

/// A value copy of the state of one agent, as seen by strategies such as task
/// allocators. Strategies never receive live Agent objects, so they cannot
/// reach into an agent while it updates.
struct AgentSnapshot {
    AgentId id;
    /// The node the agent is at (or will next arrive at).
    NodeId node;
    CapabilitySet capabilities;
    /// The task the agent is working on; invalid if it is idle.
    TaskId current_task;
};

}  // namespace mapc
