#include "mapc/allocation/task_allocator.hpp"

#include "../support/allocation_fixtures.hpp"
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mapc {
namespace {

using testing::assignmentProblem;
using testing::matrixOf;

constexpr std::nullopt_t kNo = std::nullopt;

std::vector<std::unique_ptr<TaskAllocator>> allAllocators() {
    std::vector<std::unique_ptr<TaskAllocator>> allocators;
    allocators.push_back(std::make_unique<GreedyNearestAllocator>());
    allocators.push_back(std::make_unique<CostGreedyAllocator>());
    allocators.push_back(std::make_unique<HungarianAllocator>());
    return allocators;
}

std::vector<MatrixMatch> solveChecked(const TaskAllocator& allocator, const CostMatrix& costs) {
    std::vector<MatrixMatch> matches = allocator.solve(costs);
    EXPECT_EQ(assignmentProblem(costs, matches), "") << allocator.name();
    return matches;
}

TEST(TaskAllocatorTest, Names) {
    EXPECT_EQ(GreedyNearestAllocator{}.name(), "greedy-nearest");
    EXPECT_EQ(CostGreedyAllocator{}.name(), "cost-greedy");
    EXPECT_EQ(HungarianAllocator{}.name(), "hungarian");
}

TEST(TaskAllocatorTest, EmptyInputsGiveEmptyAssignments) {
    for (const auto& allocator : allAllocators()) {
        EXPECT_TRUE(allocator->solve(CostMatrix{}).empty()) << allocator->name();
        EXPECT_TRUE(allocator->solve(CostMatrix(0, 3)).empty()) << allocator->name();
        EXPECT_TRUE(allocator->solve(CostMatrix(3, 0)).empty()) << allocator->name();
        EXPECT_TRUE(allocator->solve(CostMatrix(3, 3)).empty())
            << allocator->name();  // all infeasible
    }
}

TEST(TaskAllocatorTest, EasySquareInstanceIsSolvedByAll) {
    const CostMatrix costs = matrixOf({{4, 1}, {2, 8}});
    for (const auto& allocator : allAllocators()) {
        const auto matches = solveChecked(*allocator, costs);
        EXPECT_EQ(matches, (std::vector<MatrixMatch>{{0, 1}, {1, 0}})) << allocator->name();
    }
}

TEST(TaskAllocatorTest, GreedyCanBeSuboptimal) {
    // Taking the cheapest pair first (agent 0 -> task 0) forces agent 1 onto
    // the expensive task.
    const CostMatrix costs = matrixOf({{1, 2}, {2, 100}});
    EXPECT_EQ(totalCost(costs, solveChecked(CostGreedyAllocator{}, costs)), 101);
    EXPECT_EQ(totalCost(costs, solveChecked(GreedyNearestAllocator{}, costs)), 101);
    const auto optimal = solveChecked(HungarianAllocator{}, costs);
    EXPECT_EQ(optimal, (std::vector<MatrixMatch>{{0, 1}, {1, 0}}));
    EXPECT_EQ(totalCost(costs, optimal), 4);
}

TEST(TaskAllocatorTest, GreedyNearestServesTasksInOrder) {
    // Task 0 is served first and takes agent 0, its nearest; cost-greedy
    // instead starts from the globally cheapest pair (agent 0, task 1).
    const CostMatrix costs = matrixOf({{2, 1}, {9, 9}});
    EXPECT_EQ(solveChecked(GreedyNearestAllocator{}, costs),
              (std::vector<MatrixMatch>{{0, 0}, {1, 1}}));
    EXPECT_EQ(solveChecked(CostGreedyAllocator{}, costs),
              (std::vector<MatrixMatch>{{0, 1}, {1, 0}}));
    EXPECT_EQ(totalCost(costs, solveChecked(HungarianAllocator{}, costs)), 10);
}

TEST(TaskAllocatorTest, GreedyTiesGoToLowerIndices) {
    const CostMatrix costs = matrixOf({{5, 5}, {5, 5}});
    const std::vector<MatrixMatch> diagonal{{0, 0}, {1, 1}};
    EXPECT_EQ(solveChecked(GreedyNearestAllocator{}, costs), diagonal);
    EXPECT_EQ(solveChecked(CostGreedyAllocator{}, costs), diagonal);
}

TEST(TaskAllocatorTest, MoreTasksThanAgents) {
    const CostMatrix costs = matrixOf({
        {9, 3, 7, 1},
        {2, 8, 6, 4},
    });
    const auto matches = solveChecked(HungarianAllocator{}, costs);
    EXPECT_EQ(matches, (std::vector<MatrixMatch>{{0, 3}, {1, 0}}));
    EXPECT_EQ(totalCost(costs, matches), 3);
    for (const auto& allocator : allAllocators()) {
        EXPECT_EQ(solveChecked(*allocator, costs).size(), 2U) << allocator->name();
    }
}

TEST(TaskAllocatorTest, MoreAgentsThanTasks) {
    const CostMatrix costs = matrixOf({{9, 2}, {3, 8}, {1, 7}, {6, 6}});
    const auto matches = solveChecked(HungarianAllocator{}, costs);
    EXPECT_EQ(matches, (std::vector<MatrixMatch>{{0, 1}, {2, 0}}));
    EXPECT_EQ(totalCost(costs, matches), 3);
    for (const auto& allocator : allAllocators()) {
        EXPECT_EQ(solveChecked(*allocator, costs).size(), 2U) << allocator->name();
    }
}

TEST(TaskAllocatorTest, HungarianMaximisesTheNumberOfAssignments) {
    // The cheapest single pair (either agent on task 0) would leave agent 1
    // without a task; the optimum assigns both.
    const CostMatrix costs = matrixOf({{1, 100}, {1, kNo}});
    const auto matches = solveChecked(HungarianAllocator{}, costs);
    EXPECT_EQ(matches, (std::vector<MatrixMatch>{{0, 1}, {1, 0}}));
    EXPECT_EQ(totalCost(costs, matches), 101);
    EXPECT_EQ(solveChecked(CostGreedyAllocator{}, costs).size(), 1U);
}

TEST(TaskAllocatorTest, InfeasiblePairsAreNeverAssigned) {
    const CostMatrix costs = matrixOf({
        {kNo, kNo, kNo},
        {kNo, 5, kNo},
        {kNo, 1, kNo},
    });
    for (const auto& allocator : allAllocators()) {
        const auto matches = solveChecked(*allocator, costs);
        ASSERT_EQ(matches.size(), 1U) << allocator->name();
        EXPECT_EQ(matches.front().task, 1U) << allocator->name();
    }
    EXPECT_EQ(solveChecked(HungarianAllocator{}, costs).front().agent, 2U);
}

TEST(TaskAllocatorTest, NegativeAndExtremeCosts) {
    const TaskCost::rep big = TaskCost::kLimit;
    const CostMatrix costs = matrixOf({{-big, big, 0}, {big, -big, -3}, {-big, -big, big}});
    const auto matches = solveChecked(HungarianAllocator{}, costs);
    EXPECT_EQ(totalCost(costs, matches), testing::bruteForceOptimum(costs).cost);
    EXPECT_EQ(matches.size(), 3U);
}

// --- allocate(): ids, capabilities and the contract check ------------------

/// Looks costs up by (agent id, task id); pairs that are not listed cost 50.
class TableModel final : public CostModel {
public:
    explicit TableModel(std::map<std::pair<AgentId, TaskId>, TaskCost> table)
        : table_(std::move(table)) {}

    [[nodiscard]] TaskCost cost(const AgentSnapshot& agent, const Task& task) const override {
        const auto it = table_.find({agent.id, task.id});
        return it == table_.end() ? TaskCost::of(50) : it->second;
    }

private:
    std::map<std::pair<AgentId, TaskId>, TaskCost> table_;
};

AgentSnapshot agent(AgentId::rep_type id, CapabilitySet capabilities = {}) {
    return AgentSnapshot{AgentId{id}, NodeId{0}, capabilities, TaskId::invalid()};
}

Task task(TaskId::rep_type id, CapabilitySet required = {}) {
    Task t;
    t.id = TaskId{id};
    t.requirements.capabilities = required;
    return t;
}

TEST(TaskAllocatorTest, AllocateReportsIdsInAgentOrder) {
    const TableModel model({
        {{AgentId{7}, TaskId{20}}, TaskCost::of(1)},
        {{AgentId{3}, TaskId{10}}, TaskCost::of(2)},
    });
    const Assignment result =
        HungarianAllocator{}.allocate({agent(7), agent(3)}, {task(10), task(20)}, model);
    ASSERT_EQ(result.assigned.size(), 2U);
    EXPECT_EQ(result.assigned[0].agent, AgentId{7});
    EXPECT_EQ(result.assigned[0].task, TaskId{20});
    EXPECT_EQ(result.assigned[0].cost, 1);
    EXPECT_EQ(result.assigned[1].agent, AgentId{3});
    EXPECT_EQ(result.assigned[1].task, TaskId{10});
    EXPECT_EQ(result.assigned[1].cost, 2);
    EXPECT_EQ(result.total_cost, 3);
}

TEST(TaskAllocatorTest, AllocateRespectsCapabilities) {
    const TableModel model({});
    const CapabilitySet lift = CapabilitySet::of({0});
    // Only agent 2 can lift, so it must take task 1 even though agent 1 is
    // listed first.
    for (const auto& allocator : allAllocators()) {
        const Assignment result =
            allocator->allocate({agent(1), agent(2, lift)}, {task(1, lift)}, model);
        ASSERT_EQ(result.assigned.size(), 1U) << allocator->name();
        EXPECT_EQ(result.assigned[0].agent, AgentId{2}) << allocator->name();
    }
    EXPECT_TRUE(HungarianAllocator{}.allocate({agent(1)}, {task(1, lift)}, model).assigned.empty());
}

TEST(TaskAllocatorTest, AllocateRejectsInvalidAndDuplicateIds) {
    const TableModel model({});
    const HungarianAllocator allocator;
    EXPECT_THROW((void)allocator.allocate({agent(1), agent(1)}, {task(1)}, model),
                 std::invalid_argument);
    EXPECT_THROW((void)allocator.allocate({agent(1)}, {task(4), task(4)}, model),
                 std::invalid_argument);
    EXPECT_THROW((void)allocator.allocate({AgentSnapshot{}}, {task(1)}, model),
                 std::invalid_argument);
    EXPECT_THROW((void)allocator.allocate({agent(1)}, {Task{}}, model), std::invalid_argument);
    EXPECT_TRUE(allocator.allocate({}, {}, model).assigned.empty());
}

/// Returns a fixed (possibly invalid) answer.
class FixedAllocator final : public TaskAllocator {
public:
    explicit FixedAllocator(std::vector<MatrixMatch> answer) : answer_(std::move(answer)) {}
    [[nodiscard]] std::vector<MatrixMatch> solve(const CostMatrix& /*costs*/) const override {
        return answer_;
    }
    [[nodiscard]] std::string_view name() const noexcept override { return "fixed"; }

private:
    std::vector<MatrixMatch> answer_;
};

TEST(TaskAllocatorTest, AllocateRejectsStrategiesThatBreakTheContract) {
    const TableModel model({{{AgentId{2}, TaskId{2}}, TaskCost::infeasible()}});
    const std::vector<AgentSnapshot> agents{agent(1), agent(2)};
    const std::vector<Task> tasks{task(1), task(2)};
    const auto run = [&](std::vector<MatrixMatch> answer) {
        return FixedAllocator(std::move(answer)).allocate(agents, tasks, model);
    };
    EXPECT_EQ(run({{0, 1}, {1, 0}}).total_cost, 100);
    EXPECT_THROW((void)run({{0, 0}, {0, 1}}), std::logic_error);  // agent reused
    EXPECT_THROW((void)run({{0, 0}, {1, 0}}), std::logic_error);  // task reused
    EXPECT_THROW((void)run({{1, 0}, {0, 1}}), std::logic_error);  // not sorted
    EXPECT_THROW((void)run({{1, 1}}), std::logic_error);          // infeasible
    EXPECT_THROW((void)run({{2, 0}}), std::out_of_range);         // outside
}

}  // namespace
}  // namespace mapc
