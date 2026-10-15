#include "mapc/allocation/cost_model.hpp"

#include "mapc/allocation/task_allocator.hpp"

#include "../support/graph_fixtures.hpp"
#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace mapc {
namespace {

using testing::buildOrThrow;

TEST(TaskCostTest, FiniteAndInfeasible) {
    EXPECT_FALSE(TaskCost{}.feasible());
    EXPECT_EQ(TaskCost{}, TaskCost::infeasible());
    const TaskCost cost = TaskCost::of(-7);
    EXPECT_TRUE(cost.feasible());
    EXPECT_EQ(cost.value(), -7);
    EXPECT_NE(cost, TaskCost::infeasible());
    EXPECT_THROW((void)TaskCost::infeasible().value(), std::logic_error);
}

TEST(TaskCostTest, RangeIsLimited) {
    EXPECT_EQ(TaskCost::of(TaskCost::kLimit).value(), TaskCost::kLimit);
    EXPECT_EQ(TaskCost::of(-TaskCost::kLimit).value(), -TaskCost::kLimit);
    EXPECT_THROW((void)TaskCost::of(TaskCost::kLimit + 1), std::out_of_range);
    EXPECT_THROW((void)TaskCost::of(-TaskCost::kLimit - 1), std::out_of_range);
    EXPECT_THROW((void)TaskCost::of(std::numeric_limits<TaskCost::rep>::min()), std::out_of_range);
}

/// A 1-D corridor 0 - 1 - 2 - 3 - 4 with unit costs both ways, plus node 5
/// that can be left but not entered (5 -> 0).
class TravelCostModelTest : public ::testing::Test {
protected:
    TravelCostModelTest()
        : graph_(makeGraph()), oracle_(std::make_shared<DistanceOracle>(GraphView(graph_))) {}

    static Graph makeGraph() {
        GraphBuilder builder;
        for (int i = 0; i < 6; ++i) {
            builder.addNode();
        }
        for (NodeId::rep_type i = 0; i < 4; ++i) {
            builder.addBidirectionalEdge(NodeId{i}, NodeId{i + 1}, Duration{1});
        }
        builder.addEdge(NodeId{5}, NodeId{0}, Duration{1});
        return buildOrThrow(std::move(builder));
    }

    static AgentSnapshot agentAt(NodeId::rep_type node) {
        return AgentSnapshot{AgentId{1}, NodeId{node}, {}, TaskId::invalid()};
    }

    static Task task(NodeId::rep_type source, NodeId::rep_type destination,
                     std::int32_t priority = 0) {
        Task t;
        t.id = TaskId{1};
        t.source = NodeId{source};
        t.destination = NodeId{destination};
        t.priority = priority;
        return t;
    }

    Graph graph_;
    std::shared_ptr<DistanceOracle> oracle_;
};

TEST_F(TravelCostModelTest, ApproachPlusDelivery) {
    const TravelCostModel model(oracle_);
    EXPECT_EQ(model.cost(agentAt(0), task(3, 1)).value(), 3 + 2);
    EXPECT_EQ(model.cost(agentAt(3), task(3, 3)).value(), 0);
    EXPECT_EQ(model.cost(agentAt(5), task(4, 0)).value(), 5 + 4);
    EXPECT_EQ(&model.oracle(), oracle_.get());
}

TEST_F(TravelCostModelTest, UnreachableLegsAreInfeasible) {
    const TravelCostModel model(oracle_);
    EXPECT_FALSE(model.cost(agentAt(0), task(5, 0)).feasible());  // source unreachable
    EXPECT_FALSE(model.cost(agentAt(5), task(0, 5)).feasible());  // destination unreachable
}

TEST_F(TravelCostModelTest, PriorityWeightRewardsUrgentTasks) {
    const TravelCostModel model(oracle_, CostWeights{10});
    EXPECT_EQ(model.weights().priority_weight, 10);
    EXPECT_EQ(model.cost(agentAt(0), task(1, 2, 0)).value(), 2);
    EXPECT_EQ(model.cost(agentAt(0), task(1, 2, 3)).value(), 2 - 30);
    EXPECT_EQ(model.cost(agentAt(0), task(1, 2, -1)).value(), 2 + 10);
}

TEST_F(TravelCostModelTest, RejectsBadConfiguration) {
    EXPECT_THROW(TravelCostModel(nullptr), std::invalid_argument);
    EXPECT_THROW(TravelCostModel(oracle_, CostWeights{-1}), std::invalid_argument);
    EXPECT_THROW(TravelCostModel(oracle_, CostWeights{CostWeights::kMaxPriorityWeight + 1}),
                 std::invalid_argument);
    EXPECT_NO_THROW(TravelCostModel(oracle_, CostWeights{CostWeights::kMaxPriorityWeight}));
}

TEST_F(TravelCostModelTest, CostsBeyondTheLimitThrow) {
    // 2^20 * 2^31 > 2^40: an extreme priority overflows the TaskCost range.
    const TravelCostModel model(oracle_, CostWeights{CostWeights::kMaxPriorityWeight});
    EXPECT_THROW((void)model.cost(agentAt(0), task(1, 2, std::numeric_limits<std::int32_t>::max())),
                 std::overflow_error);
    EXPECT_THROW((void)model.cost(agentAt(0), task(1, 2, std::numeric_limits<std::int32_t>::min())),
                 std::overflow_error);
}

TEST(TravelCostModelLimitTest, HugeTravelTimesThrow) {
    GraphBuilder builder;
    for (int i = 0; i < 3; ++i) {
        builder.addNode();
    }
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{TaskCost::kLimit + 1});
    builder.addEdge(NodeId{1}, NodeId{2}, Duration{TaskCost::kLimit});
    builder.addEdge(NodeId{2}, NodeId{1}, Duration{1});
    const Graph graph = buildOrThrow(std::move(builder));
    const TravelCostModel model(std::make_shared<DistanceOracle>(GraphView(graph)));
    const auto at = [](NodeId::rep_type node) {
        return AgentSnapshot{AgentId{0}, NodeId{node}, {}, {}};
    };
    Task task;
    // One leg beyond the limit.
    task.source = NodeId{1};
    task.destination = NodeId{1};
    EXPECT_THROW((void)model.cost(at(0), task), std::overflow_error);
    // Exactly at the limit.
    task.source = NodeId{2};
    task.destination = NodeId{2};
    EXPECT_EQ(model.cost(at(1), task).value(), TaskCost::kLimit);
    // Each leg within the limit, but not their sum.
    task.destination = NodeId{1};
    EXPECT_THROW((void)model.cost(at(1), task), std::overflow_error);
}

TEST_F(TravelCostModelTest, UnknownNodesThrow) {
    const TravelCostModel model(oracle_);
    EXPECT_THROW((void)model.cost(agentAt(9), task(0, 1)), std::out_of_range);
    EXPECT_THROW((void)model.cost(agentAt(0), task(0, 9)), std::out_of_range);
}

/// Returns agent index * 10 + task index and counts how often it is asked.
class CountingModel final : public CostModel {
public:
    [[nodiscard]] TaskCost cost(const AgentSnapshot& agent, const Task& task) const override {
        ++calls;
        return TaskCost::of((TaskCost::rep{agent.id.value()} * 10) + task.id.value());
    }
    mutable int calls = 0;
};

TEST(CostMatrixTest, BuildFiltersCapabilitiesWithoutAskingTheModel) {
    const std::vector<AgentSnapshot> agents{
        {AgentId{0}, NodeId{0}, CapabilitySet::of({1}), {}},
        {AgentId{1}, NodeId{0}, CapabilitySet::of({1, 2}), {}},
    };
    std::vector<Task> tasks(3);
    for (std::size_t t = 0; t < tasks.size(); ++t) {
        tasks[t].id = TaskId{static_cast<TaskId::rep_type>(t)};
    }
    tasks[1].requirements.capabilities = CapabilitySet::of({2});
    tasks[2].requirements.capabilities = CapabilitySet::of({1, 2});

    const CountingModel model;
    const CostMatrix matrix = CostMatrix::build(agents, tasks, model);
    ASSERT_EQ(matrix.agentCount(), 2U);
    ASSERT_EQ(matrix.taskCount(), 3U);
    EXPECT_EQ(matrix.at(0, 0).value(), 0);
    EXPECT_FALSE(matrix.at(0, 1).feasible());
    EXPECT_FALSE(matrix.at(0, 2).feasible());
    EXPECT_EQ(matrix.at(1, 0).value(), 10);
    EXPECT_EQ(matrix.at(1, 1).value(), 11);
    EXPECT_EQ(matrix.at(1, 2).value(), 12);
    EXPECT_EQ(model.calls, 4);
}

TEST(CostMatrixTest, AccessIsBoundsChecked) {
    CostMatrix matrix(2, 3);
    EXPECT_FALSE(matrix.at(1, 2).feasible());
    matrix.set(1, 2, TaskCost::of(4));
    EXPECT_EQ(matrix.at(1, 2).value(), 4);
    EXPECT_THROW((void)matrix.at(2, 0), std::out_of_range);
    EXPECT_THROW((void)matrix.at(0, 3), std::out_of_range);
    EXPECT_THROW(matrix.set(0, 3, TaskCost::of(1)), std::out_of_range);

    const CostMatrix empty;
    EXPECT_EQ(empty.agentCount(), 0U);
    EXPECT_EQ(empty.taskCount(), 0U);
}

TEST(CostMatrixTest, SidesAreLimited) {
    EXPECT_THROW(CostMatrix(CostMatrix::kMaxSide + 1, 0), std::length_error);
    EXPECT_THROW(CostMatrix(0, CostMatrix::kMaxSide + 1), std::length_error);
    EXPECT_NO_THROW(CostMatrix(CostMatrix::kMaxSide, 0));
}

TEST(CostMatrixTest, TotalCostSumsMatches) {
    CostMatrix matrix(2, 2);
    matrix.set(0, 1, TaskCost::of(3));
    matrix.set(1, 0, TaskCost::of(-5));
    EXPECT_EQ(totalCost(matrix, {}), 0);
    EXPECT_EQ(totalCost(matrix, {{0, 1}, {1, 0}}), -2);
    EXPECT_THROW((void)totalCost(matrix, {{0, 0}}), std::logic_error);
    EXPECT_THROW((void)totalCost(matrix, {{2, 0}}), std::out_of_range);
}

}  // namespace
}  // namespace mapc
