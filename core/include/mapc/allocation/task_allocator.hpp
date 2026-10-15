#pragma once

#include "mapc/allocation/cost_model.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/world/agent_snapshot.hpp"
#include "mapc/world/task.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace mapc {

/// The cost of every (agent, task) pair: row = agent index, column = task
/// index, in the order the agents and tasks were given.
class CostMatrix {
public:
    /// Bound on each side, which keeps every sum an allocator forms within
    /// 64 bits (2^20 pairs of at most 2^40 each).
    static constexpr std::size_t kMaxSide = std::size_t{1} << 20;

    /// An empty 0 x 0 matrix.
    CostMatrix() = default;

    /// An `agents` x `tasks` matrix with every pair infeasible. Throws
    /// std::length_error if either side exceeds kMaxSide.
    CostMatrix(std::size_t agents, std::size_t tasks);

    /// Asks `model` for the cost of every pair. A pair is infeasible without
    /// asking the model if the agent lacks a capability the task requires.
    [[nodiscard]] static CostMatrix build(const std::vector<AgentSnapshot>& agents,
                                          const std::vector<Task>& tasks, const CostModel& model);

    [[nodiscard]] std::size_t agentCount() const noexcept { return agents_; }
    [[nodiscard]] std::size_t taskCount() const noexcept { return tasks_; }

    /// Throw std::out_of_range for indices outside the matrix.
    [[nodiscard]] TaskCost at(std::size_t agent, std::size_t task) const;
    void set(std::size_t agent, std::size_t task, TaskCost cost);

private:
    [[nodiscard]] std::size_t index(std::size_t agent, std::size_t task) const;

    std::size_t agents_ = 0;
    std::size_t tasks_ = 0;
    std::vector<TaskCost> costs_;
};

/// One assigned pair, by row and column of a CostMatrix.
struct MatrixMatch {
    std::size_t agent = 0;
    std::size_t task = 0;

    friend bool operator==(MatrixMatch a, MatrixMatch b) noexcept {
        return a.agent == b.agent && a.task == b.task;
    }
    friend bool operator!=(MatrixMatch a, MatrixMatch b) noexcept { return !(a == b); }
};

/// Sum of the costs of `matches`. Throws std::logic_error if a match is
/// infeasible, and std::out_of_range if it is outside the matrix.
[[nodiscard]] TaskCost::rep totalCost(const CostMatrix& costs,
                                      const std::vector<MatrixMatch>& matches);

struct AssignedTask {
    AgentId agent;
    TaskId task;
    TaskCost::rep cost = 0;
};

/// The result of one allocation round. Agents and tasks that do not appear
/// are left unassigned.
struct Assignment {
    /// Ordered as the agents were given.
    std::vector<AssignedTask> assigned;
    TaskCost::rep total_cost = 0;
};

/// A task allocation strategy.
///
/// Every agent it is given is treated as available, so callers pass idle
/// agents and pending tasks. Each agent gets at most one task and each task
/// at most one agent, and infeasible pairs are never assigned. Strategies are
/// stateless and deterministic: the same input always gives the same result.
class TaskAllocator {
public:
    TaskAllocator() = default;
    TaskAllocator(const TaskAllocator&) = delete;
    TaskAllocator& operator=(const TaskAllocator&) = delete;
    TaskAllocator(TaskAllocator&&) = delete;
    TaskAllocator& operator=(TaskAllocator&&) = delete;
    virtual ~TaskAllocator() = default;

    /// Builds the cost matrix with `model` (see CostMatrix::build) and solves
    /// it. Throws std::invalid_argument for an invalid or duplicate agent or
    /// task id.
    [[nodiscard]] Assignment allocate(const std::vector<AgentSnapshot>& agents,
                                      const std::vector<Task>& tasks, const CostModel& model) const;

    /// Solves a cost matrix directly. The result holds only feasible pairs,
    /// uses each row and column at most once, and is sorted by agent index.
    [[nodiscard]] virtual std::vector<MatrixMatch> solve(const CostMatrix& costs) const = 0;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

/// Baseline: tasks, in the order given, each take the cheapest agent that is
/// still free (ties go to the lower agent index). With TravelCostModel the
/// only agent-dependent part of a task's cost is the approach distance, so
/// this is "nearest free agent first". Callers order the tasks, for example by
/// priority and arrival. O(A * T).
class GreedyNearestAllocator final : public TaskAllocator {
public:
    [[nodiscard]] std::vector<MatrixMatch> solve(const CostMatrix& costs) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "greedy-nearest"; }
};

/// Repeatedly assigns the globally cheapest remaining (agent, task) pair.
/// Ties go to the lower agent index, then the lower task index, so the order
/// of the tasks only matters between equal costs (unlike
/// GreedyNearestAllocator, where it decides which task is served first).
/// O(A * T * log(A * T)).
class CostGreedyAllocator final : public TaskAllocator {
public:
    [[nodiscard]] std::vector<MatrixMatch> solve(const CostMatrix& costs) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "cost-greedy"; }
};

/// Optimal one-shot assignment (Kuhn-Munkres with potentials).
///
/// Assigns as many pairs as possible (min(A, T) when every pair is feasible),
/// and among those assignments minimises the total cost. Rectangular matrices
/// are solved directly rather than padded to square: O(n^2 * m) time for
/// n = min(A, T) and m = max(A, T), so a few agents never pay for a long task
/// list squared. Infeasible pairs are handled exactly, not as a large finite
/// cost.
class HungarianAllocator final : public TaskAllocator {
public:
    [[nodiscard]] std::vector<MatrixMatch> solve(const CostMatrix& costs) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "hungarian"; }
};

}  // namespace mapc
