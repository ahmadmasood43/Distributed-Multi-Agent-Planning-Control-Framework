#pragma once

#include "mapc/allocation/task_allocator.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mapc::testing {

/// Rows are agents, columns are tasks; nullopt marks an infeasible pair.
using CostRows = std::vector<std::vector<std::optional<TaskCost::rep>>>;

inline CostMatrix matrixOf(const CostRows& rows) {
    const std::size_t tasks = rows.empty() ? 0 : rows.front().size();
    CostMatrix matrix(rows.size(), tasks);
    for (std::size_t a = 0; a < rows.size(); ++a) {
        for (std::size_t t = 0; t < tasks; ++t) {
            if (const auto& cost = rows[a].at(t)) {
                matrix.set(a, t, TaskCost::of(*cost));
            }
        }
    }
    return matrix;
}

/// The value of an assignment: more pairs is better, then a lower total cost.
struct AssignmentValue {
    std::size_t pairs = 0;
    TaskCost::rep cost = 0;

    friend bool operator==(AssignmentValue a, AssignmentValue b) noexcept {
        return a.pairs == b.pairs && a.cost == b.cost;
    }
    /// True if `a` is strictly better than `b`.
    friend bool better(AssignmentValue a, AssignmentValue b) noexcept {
        return a.pairs != b.pairs ? a.pairs > b.pairs : a.cost < b.cost;
    }
};

/// Empty string if `matches` obeys the TaskAllocator::solve contract for
/// `costs`, otherwise the first problem.
inline std::string assignmentProblem(const CostMatrix& costs,
                                     const std::vector<MatrixMatch>& matches) {
    std::vector<bool> agent_used(costs.agentCount(), false);
    std::vector<bool> task_used(costs.taskCount(), false);
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const MatrixMatch match = matches[i];
        if (match.agent >= costs.agentCount() || match.task >= costs.taskCount()) {
            return "match " + std::to_string(i) + " is outside the matrix";
        }
        if (!costs.at(match.agent, match.task).feasible()) {
            return "match " + std::to_string(i) + " is infeasible";
        }
        if (agent_used[match.agent] || task_used[match.task]) {
            return "match " + std::to_string(i) + " reuses an agent or a task";
        }
        if (i > 0 && matches[i - 1].agent >= match.agent) {
            return "matches are not sorted by agent";
        }
        agent_used[match.agent] = true;
        task_used[match.task] = true;
    }
    return "";
}

inline AssignmentValue valueOf(const CostMatrix& costs, const std::vector<MatrixMatch>& matches) {
    return {matches.size(), totalCost(costs, matches)};
}

/// Exhaustive reference: every agent either takes a free feasible task or
/// stays unassigned. Exponential; for small matrices only.
inline AssignmentValue bruteForceOptimum(const CostMatrix& costs) {
    std::vector<bool> task_used(costs.taskCount(), false);
    AssignmentValue best;
    AssignmentValue current;
    auto visit = [&](auto& self, std::size_t agent) -> void {
        if (agent == costs.agentCount()) {
            if (better(current, best)) {
                best = current;
            }
            return;
        }
        self(self, agent + 1);  // leave this agent unassigned
        for (std::size_t t = 0; t < costs.taskCount(); ++t) {
            const TaskCost cost = costs.at(agent, t);
            if (task_used[t] || !cost.feasible()) {
                continue;
            }
            task_used[t] = true;
            ++current.pairs;
            current.cost += cost.value();
            self(self, agent + 1);
            current.cost -= cost.value();
            --current.pairs;
            task_used[t] = false;
        }
    };
    visit(visit, 0);
    return best;
}

}  // namespace mapc::testing
