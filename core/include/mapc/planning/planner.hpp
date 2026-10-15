#pragma once

#include "mapc/common/result.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/graph_overlay.hpp"
#include "mapc/planning/path.hpp"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace mapc {

/// Budgets that bound a single search. Deterministic by design: a wall-clock
/// timeout would make results depend on machine load.
struct SearchLimits {
    /// Maximum number of node expansions; 0 means unlimited.
    std::uint64_t max_expansions = 0;
};

struct PlanRequest {
    AgentId agent;
    NodeId start;
    NodeId goal;
    Tick start_time = Tick::zero();
    SearchLimits limits;
};

enum class PlanFailure : std::uint8_t {
    InvalidRequest,  ///< start or goal is not a node of the graph
    NoPath,          ///< the goal is unreachable (with the overlay applied)
    LimitReached,    ///< a SearchLimits budget ran out before the goal was found
};

[[nodiscard]] std::string_view toString(PlanFailure failure) noexcept;

/// Counters describing one search. Everything except `elapsed` is
/// deterministic for a given graph, overlay and request.
struct SearchStats {
    std::uint64_t expanded = 0;   ///< nodes removed from the open list and expanded
    std::uint64_t generated = 0;  ///< open-list insertions (including improvements)
    std::uint64_t max_open = 0;   ///< largest open-list size
    std::chrono::nanoseconds elapsed{0};
};

struct PlanResult {
    Result<Path, PlanFailure> outcome;
    SearchStats stats;

    [[nodiscard]] bool found() const noexcept { return outcome.hasValue(); }
};

/// A single-agent path planner.
///
/// Implementations may keep scratch memory between calls to avoid
/// reallocating per query, so a Planner instance is not thread-safe: give
/// each worker thread its own instance. Different instances may search the
/// same Graph concurrently.
class Planner {
public:
    Planner() = default;
    Planner(const Planner&) = delete;
    Planner& operator=(const Planner&) = delete;
    Planner(Planner&&) = delete;
    Planner& operator=(Planner&&) = delete;
    virtual ~Planner() = default;

    /// Plans a path in `view` (graph plus optional overlay).
    [[nodiscard]] virtual PlanResult plan(const GraphView& view, const PlanRequest& request) = 0;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

}  // namespace mapc
