#pragma once

#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"

#include <vector>

namespace mapc {

/// One step of a path: being at `node` at `time`.
struct PathStep {
    NodeId node;
    Tick time;

    friend bool operator==(const PathStep& a, const PathStep& b) noexcept {
        return a.node == b.node && a.time == b.time;
    }
    friend bool operator!=(const PathStep& a, const PathStep& b) noexcept { return !(a == b); }
};

/// A timed path through a graph.
///
/// `steps` starts at the request's start node and start time and ends at the
/// goal; consecutive steps are joined by an edge, and each step's time is the
/// previous time plus that edge's effective cost. `cost` is the total, i.e.
/// the last step's time minus the first's.
struct Path {
    std::vector<PathStep> steps;
    Duration cost = Duration::zero();

    [[nodiscard]] bool empty() const noexcept { return steps.empty(); }

    friend bool operator==(const Path& a, const Path& b) {
        return a.cost == b.cost && a.steps == b.steps;
    }
    friend bool operator!=(const Path& a, const Path& b) { return !(a == b); }
};

}  // namespace mapc
