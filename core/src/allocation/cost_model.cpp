#include "mapc/allocation/cost_model.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace mapc {

TaskCost TaskCost::of(rep value) {
    if (value < -kLimit || value > kLimit) {
        throw std::out_of_range("TaskCost: " + std::to_string(value) + " is outside [-2^40, 2^40]");
    }
    return TaskCost{value};
}

TaskCost::rep TaskCost::value() const {
    if (!feasible()) {
        throw std::logic_error("TaskCost: an infeasible cost has no value");
    }
    return value_;
}

TravelCostModel::TravelCostModel(std::shared_ptr<DistanceOracle> oracle, CostWeights weights)
    : oracle_(std::move(oracle)), weights_(weights) {
    if (!oracle_) {
        throw std::invalid_argument("TravelCostModel: the distance oracle is null");
    }
    if (weights_.priority_weight < 0 ||
        weights_.priority_weight > CostWeights::kMaxPriorityWeight) {
        throw std::invalid_argument("TravelCostModel: priority_weight must be in [0, 2^20]");
    }
}

TaskCost TravelCostModel::cost(const AgentSnapshot& agent, const Task& task) const {
    const std::optional<Duration> approach = oracle_->distance(agent.node, task.source);
    if (!approach) {
        return TaskCost::infeasible();
    }
    const std::optional<Duration> delivery = oracle_->distance(task.source, task.destination);
    if (!delivery) {
        return TaskCost::infeasible();
    }
    if (approach->count() > TaskCost::kLimit || delivery->count() > TaskCost::kLimit) {
        throw std::overflow_error("TravelCostModel: travel time exceeds TaskCost::kLimit");
    }
    // |priority_weight * priority| <= 2^20 * 2^31, so nothing below overflows.
    const TaskCost::rep total = approach->count() + delivery->count() -
                                weights_.priority_weight * TaskCost::rep{task.priority};
    if (total < -TaskCost::kLimit || total > TaskCost::kLimit) {
        throw std::overflow_error("TravelCostModel: cost exceeds TaskCost::kLimit");
    }
    return TaskCost::of(total);
}

}  // namespace mapc
