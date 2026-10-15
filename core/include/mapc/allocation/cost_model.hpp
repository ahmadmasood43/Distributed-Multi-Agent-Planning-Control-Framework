#pragma once

#include "mapc/allocation/distance_oracle.hpp"
#include "mapc/world/agent_snapshot.hpp"
#include "mapc/world/task.hpp"

#include <cstdint>
#include <limits>
#include <memory>

namespace mapc {

/// The estimated cost of one agent carrying out one task: either a finite
/// number of ticks (penalties and bonuses are expressed in ticks too), or
/// infeasible.
///
/// Finite costs are limited to [-kLimit, kLimit] so that allocators can add
/// up many of them in 64-bit arithmetic without overflowing.
class TaskCost {
public:
    using rep = std::int64_t;

    /// 2^40 ticks: about 35 years at one tick per millisecond.
    static constexpr rep kLimit = rep{1} << 40;

    /// Infeasible: the pair is never assigned.
    constexpr TaskCost() noexcept = default;

    [[nodiscard]] static constexpr TaskCost infeasible() noexcept { return TaskCost{}; }

    /// A finite cost. Throws std::out_of_range if |value| > kLimit.
    [[nodiscard]] static TaskCost of(rep value);

    [[nodiscard]] constexpr bool feasible() const noexcept { return value_ != kInfeasible; }

    /// The finite value. Throws std::logic_error if the cost is infeasible.
    [[nodiscard]] rep value() const;

    friend constexpr bool operator==(TaskCost a, TaskCost b) noexcept {
        return a.value_ == b.value_;
    }
    friend constexpr bool operator!=(TaskCost a, TaskCost b) noexcept {
        return a.value_ != b.value_;
    }

private:
    static constexpr rep kInfeasible = std::numeric_limits<rep>::min();

    constexpr explicit TaskCost(rep value) noexcept : value_(value) {}

    rep value_ = kInfeasible;
};

/// Estimates what it costs an agent to carry out a task. Allocators minimise
/// the sum of these estimates.
///
/// Capability mismatches never reach a cost model: CostMatrix::build marks
/// them infeasible itself.
class CostModel {
public:
    CostModel() = default;
    CostModel(const CostModel&) = delete;
    CostModel& operator=(const CostModel&) = delete;
    CostModel(CostModel&&) = delete;
    CostModel& operator=(CostModel&&) = delete;
    virtual ~CostModel() = default;

    [[nodiscard]] virtual TaskCost cost(const AgentSnapshot& agent, const Task& task) const = 0;
};

/// Weights of the terms of TravelCostModel.
struct CostWeights {
    /// Ticks subtracted per priority level, so urgent tasks win when there
    /// are more tasks than agents. Must be in [0, kMaxPriorityWeight].
    TaskCost::rep priority_weight = 0;

    static constexpr TaskCost::rep kMaxPriorityWeight = TaskCost::rep{1} << 20;
};

/// Cost = travel time from the agent to the task source + travel time from
/// the source to the destination - priority_weight * priority.
///
/// Travel times are exact shortest-path distances from a DistanceOracle, so
/// allocators never run a planner. A pair is infeasible if the source cannot
/// be reached from the agent, or the destination from the source. Deadline
/// penalties arrive with deadline-aware allocation in M8.
///
/// Not thread-safe: the oracle fills its cache while answering.
class TravelCostModel final : public CostModel {
public:
    /// Throws std::invalid_argument for a null oracle or out-of-range
    /// weights.
    explicit TravelCostModel(std::shared_ptr<DistanceOracle> oracle, CostWeights weights = {});

    /// Throws std::out_of_range for a node that is not in the oracle's graph,
    /// and std::overflow_error if either travel leg, or the resulting cost,
    /// exceeds TaskCost::kLimit in magnitude (a leg beyond the limit throws
    /// even if a priority bonus would bring the total back within it).
    [[nodiscard]] TaskCost cost(const AgentSnapshot& agent, const Task& task) const override;

    [[nodiscard]] const CostWeights& weights() const noexcept { return weights_; }
    [[nodiscard]] DistanceOracle& oracle() const noexcept { return *oracle_; }

private:
    std::shared_ptr<DistanceOracle> oracle_;
    CostWeights weights_;
};

}  // namespace mapc
