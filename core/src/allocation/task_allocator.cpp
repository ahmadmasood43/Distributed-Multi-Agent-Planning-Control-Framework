#include "mapc/allocation/task_allocator.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <tuple>

namespace mapc {

namespace {

template <typename Id>
void requireUniqueValidIds(std::vector<Id> ids, const char* what) {
    for (const Id id : ids) {
        if (!id.valid()) {
            throw std::invalid_argument(std::string("TaskAllocator: invalid ") + what + " id");
        }
    }
    std::sort(ids.begin(), ids.end());
    if (const auto dup = std::adjacent_find(ids.begin(), ids.end()); dup != ids.end()) {
        throw std::invalid_argument("TaskAllocator: duplicate " + toString(*dup));
    }
}

}  // namespace

// --- CostMatrix -------------------------------------------------------------

CostMatrix::CostMatrix(std::size_t agents, std::size_t tasks) : agents_(agents), tasks_(tasks) {
    if (agents > kMaxSide || tasks > kMaxSide) {
        throw std::length_error("CostMatrix: at most 2^20 agents and 2^20 tasks");
    }
    costs_.assign(agents * tasks, TaskCost::infeasible());
}

CostMatrix CostMatrix::build(const std::vector<AgentSnapshot>& agents,
                             const std::vector<Task>& tasks, const CostModel& model) {
    CostMatrix matrix(agents.size(), tasks.size());
    for (std::size_t a = 0; a < agents.size(); ++a) {
        for (std::size_t t = 0; t < tasks.size(); ++t) {
            if (agents[a].capabilities.containsAll(tasks[t].requirements.capabilities)) {
                matrix.costs_[(a * matrix.tasks_) + t] = model.cost(agents[a], tasks[t]);
            }
        }
    }
    return matrix;
}

std::size_t CostMatrix::index(std::size_t agent, std::size_t task) const {
    if (agent >= agents_ || task >= tasks_) {
        throw std::out_of_range("CostMatrix: (" + std::to_string(agent) + ", " +
                                std::to_string(task) + ") is outside the " +
                                std::to_string(agents_) + " x " + std::to_string(tasks_) +
                                " matrix");
    }
    return (agent * tasks_) + task;
}

TaskCost CostMatrix::at(std::size_t agent, std::size_t task) const {
    return costs_[index(agent, task)];
}

void CostMatrix::set(std::size_t agent, std::size_t task, TaskCost cost) {
    costs_[index(agent, task)] = cost;
}

TaskCost::rep totalCost(const CostMatrix& costs, const std::vector<MatrixMatch>& matches) {
    TaskCost::rep total = 0;
    for (const MatrixMatch match : matches) {
        total += costs.at(match.agent, match.task).value();
    }
    return total;
}

// --- TaskAllocator ----------------------------------------------------------

Assignment TaskAllocator::allocate(const std::vector<AgentSnapshot>& agents,
                                   const std::vector<Task>& tasks, const CostModel& model) const {
    std::vector<AgentId> agent_ids;
    agent_ids.reserve(agents.size());
    for (const AgentSnapshot& agent : agents) {
        agent_ids.push_back(agent.id);
    }
    requireUniqueValidIds(std::move(agent_ids), "agent");
    std::vector<TaskId> task_ids;
    task_ids.reserve(tasks.size());
    for (const Task& task : tasks) {
        task_ids.push_back(task.id);
    }
    requireUniqueValidIds(std::move(task_ids), "task");

    const CostMatrix costs = CostMatrix::build(agents, tasks, model);
    const std::vector<MatrixMatch> matches = solve(costs);

    // Hold every strategy to the contract, so a broken one fails loudly here
    // instead of double-booking an agent.
    std::vector<bool> agent_used(agents.size(), false);
    std::vector<bool> task_used(tasks.size(), false);
    Assignment result;
    result.assigned.reserve(matches.size());
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const MatrixMatch match = matches[i];
        const TaskCost cost = costs.at(match.agent, match.task);
        if (!cost.feasible() || agent_used[match.agent] || task_used[match.task] ||
            (i > 0 && matches[i - 1].agent >= match.agent)) {
            throw std::logic_error("TaskAllocator: " + std::string(name()) +
                                   " returned an invalid assignment");
        }
        agent_used[match.agent] = true;
        task_used[match.task] = true;
        result.assigned.push_back({agents[match.agent].id, tasks[match.task].id, cost.value()});
        result.total_cost += cost.value();
    }
    return result;
}

// --- Greedy strategies ------------------------------------------------------

std::vector<MatrixMatch> GreedyNearestAllocator::solve(const CostMatrix& costs) const {
    std::vector<bool> agent_used(costs.agentCount(), false);
    std::vector<MatrixMatch> matches;
    for (std::size_t t = 0; t < costs.taskCount(); ++t) {
        std::size_t best = costs.agentCount();
        TaskCost::rep best_cost = 0;
        for (std::size_t a = 0; a < costs.agentCount(); ++a) {
            const TaskCost cost = costs.at(a, t);
            if (agent_used[a] || !cost.feasible()) {
                continue;
            }
            if (best == costs.agentCount() || cost.value() < best_cost) {
                best = a;
                best_cost = cost.value();
            }
        }
        if (best != costs.agentCount()) {
            agent_used[best] = true;
            matches.push_back({best, t});
        }
    }
    std::sort(matches.begin(), matches.end(),
              [](MatrixMatch x, MatrixMatch y) { return x.agent < y.agent; });
    return matches;
}

std::vector<MatrixMatch> CostGreedyAllocator::solve(const CostMatrix& costs) const {
    std::vector<std::tuple<TaskCost::rep, std::size_t, std::size_t>> pairs;
    for (std::size_t a = 0; a < costs.agentCount(); ++a) {
        for (std::size_t t = 0; t < costs.taskCount(); ++t) {
            if (const TaskCost cost = costs.at(a, t); cost.feasible()) {
                pairs.emplace_back(cost.value(), a, t);
            }
        }
    }
    std::sort(pairs.begin(), pairs.end());
    std::vector<bool> agent_used(costs.agentCount(), false);
    std::vector<bool> task_used(costs.taskCount(), false);
    std::vector<MatrixMatch> matches;
    for (const auto& pair : pairs) {
        const std::size_t a = std::get<1>(pair);
        const std::size_t t = std::get<2>(pair);
        if (!agent_used[a] && !task_used[t]) {
            agent_used[a] = true;
            task_used[t] = true;
            matches.push_back({a, t});
        }
    }
    std::sort(matches.begin(), matches.end(),
              [](MatrixMatch x, MatrixMatch y) { return x.agent < y.agent; });
    return matches;
}

}  // namespace mapc
