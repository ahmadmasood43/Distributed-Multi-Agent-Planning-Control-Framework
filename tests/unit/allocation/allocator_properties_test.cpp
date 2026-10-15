// Property tests over seeded random cost matrices. They implement the M2
// acceptance criteria: the Hungarian allocator matches an exhaustive search on
// every instance with up to 7 agents and 7 tasks (square and rectangular, with
// infeasible pairs and negative or extreme costs), and the greedy strategies
// always return valid assignments that are never better than the optimum.

#include "mapc/allocation/task_allocator.hpp"
#include "mapc/common/rng.hpp"

#include "../support/allocation_fixtures.hpp"
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace mapc {
namespace {

using testing::assignmentProblem;
using testing::AssignmentValue;
using testing::bruteForceOptimum;
using testing::valueOf;

constexpr std::uint64_t kSeed = 20261016;
constexpr int kInstances = 3000;

/// Cost distributions, so ties, sign changes and the extremes of the
/// TaskCost range are all exercised.
CostMatrix randomMatrix(Rng& rng, int index) {
    const auto agents = static_cast<std::size_t>(rng.uniformInt(0, 7));
    const auto tasks = static_cast<std::size_t>(rng.uniformInt(0, 7));
    const int infeasible_percent = static_cast<int>(rng.uniformInt(0, 3)) * 20;  // 0..60
    CostMatrix costs(agents, tasks);
    for (std::size_t a = 0; a < agents; ++a) {
        for (std::size_t t = 0; t < tasks; ++t) {
            if (rng.uniformInt(0, 99) < infeasible_percent) {
                continue;
            }
            TaskCost::rep cost = 0;
            switch (index % 4) {
                case 0:  // small: many ties
                    cost = rng.uniformInt(0, 5);
                    break;
                case 1:  // travel-like
                    cost = rng.uniformInt(0, 1000);
                    break;
                case 2:  // negative too (priority bonuses)
                    cost = rng.uniformInt(-1000, 1000);
                    break;
                default:  // the extremes of the range
                    cost = rng.uniformInt(0, 1) == 0
                               ? rng.uniformInt(-3, 3)
                               : (rng.uniformInt(0, 1) == 0 ? -1 : 1) *
                                     (TaskCost::kLimit - rng.uniformInt(0, 3));
                    break;
            }
            costs.set(a, t, TaskCost::of(cost));
        }
    }
    return costs;
}

CostMatrix transpose(const CostMatrix& costs) {
    CostMatrix out(costs.taskCount(), costs.agentCount());
    for (std::size_t a = 0; a < costs.agentCount(); ++a) {
        for (std::size_t t = 0; t < costs.taskCount(); ++t) {
            out.set(t, a, costs.at(a, t));
        }
    }
    return out;
}

TEST(AllocatorPropertiesTest, HungarianMatchesBruteForceUpToSeven) {
    Rng rng(kSeed);
    const HungarianAllocator hungarian;
    int square = 0;
    int wide = 0;
    int tall = 0;
    for (int i = 0; i < kInstances; ++i) {
        const CostMatrix costs = randomMatrix(rng, i);
        square += costs.agentCount() == costs.taskCount() ? 1 : 0;
        wide += costs.agentCount() < costs.taskCount() ? 1 : 0;
        tall += costs.agentCount() > costs.taskCount() ? 1 : 0;

        const auto matches = hungarian.solve(costs);
        ASSERT_EQ(assignmentProblem(costs, matches), "") << "instance " << i;
        const AssignmentValue optimum = bruteForceOptimum(costs);
        const AssignmentValue got = valueOf(costs, matches);
        ASSERT_EQ(got.pairs, optimum.pairs) << "instance " << i;
        ASSERT_EQ(got.cost, optimum.cost) << "instance " << i;

        // Swapping the roles of agents and tasks must not change the optimum.
        const CostMatrix flipped = transpose(costs);
        const auto flipped_matches = hungarian.solve(flipped);
        ASSERT_EQ(assignmentProblem(flipped, flipped_matches), "") << "instance " << i;
        ASSERT_EQ(valueOf(flipped, flipped_matches), optimum) << "instance " << i;
    }
    // Every shape is well represented.
    EXPECT_GT(square, kInstances / 10);
    EXPECT_GT(wide, kInstances / 3);
    EXPECT_GT(tall, kInstances / 3);
}

TEST(AllocatorPropertiesTest, GreedyStrategiesAreValidButNeverBetterThanOptimal) {
    Rng rng(kSeed + 1);
    const HungarianAllocator hungarian;
    const GreedyNearestAllocator nearest;
    const CostGreedyAllocator cost_greedy;
    int nearest_worse = 0;
    int cost_greedy_worse = 0;
    for (int i = 0; i < kInstances; ++i) {
        const CostMatrix costs = randomMatrix(rng, i);
        const AssignmentValue optimum = valueOf(costs, hungarian.solve(costs));
        for (const TaskAllocator* allocator :
             std::vector<const TaskAllocator*>{&nearest, &cost_greedy}) {
            const auto matches = allocator->solve(costs);
            ASSERT_EQ(assignmentProblem(costs, matches), "")
                << allocator->name() << ", instance " << i;
            const AssignmentValue value = valueOf(costs, matches);
            ASSERT_FALSE(better(value, optimum)) << allocator->name() << ", instance " << i;
            if (better(optimum, value)) {
                ++(allocator == &nearest ? nearest_worse : cost_greedy_worse);
            }
            // Same input, same answer.
            ASSERT_EQ(allocator->solve(costs), matches) << allocator->name();
        }
    }
    // The greedy strategies really are approximations on these instances.
    EXPECT_GT(nearest_worse, 0);
    EXPECT_GT(cost_greedy_worse, 0);
}

// Larger instances at the edges of the cost range, with many infeasible pairs.
// No exhaustive reference exists at this size, but the result must be valid,
// independent of orientation and at least as good as both greedy answers; in
// the sanitizer builds any 64-bit overflow inside the solver would abort.
TEST(AllocatorPropertiesTest, LargeExtremeInstancesStayConsistent) {
    Rng rng(kSeed + 2);
    const HungarianAllocator hungarian;
    for (int i = 0; i < 40; ++i) {
        const auto agents = static_cast<std::size_t>(rng.uniformInt(1, 60));
        const auto tasks = static_cast<std::size_t>(rng.uniformInt(1, 60));
        CostMatrix costs(agents, tasks);
        for (std::size_t a = 0; a < agents; ++a) {
            for (std::size_t t = 0; t < tasks; ++t) {
                if (rng.uniformInt(0, 99) < 50) {
                    const TaskCost::rep sign = rng.uniformInt(0, 1) == 0 ? -1 : 1;
                    costs.set(a, t, TaskCost::of(sign * (TaskCost::kLimit - rng.uniformInt(0, 9))));
                }
            }
        }
        const auto matches = hungarian.solve(costs);
        ASSERT_EQ(assignmentProblem(costs, matches), "") << "instance " << i;
        const AssignmentValue value = valueOf(costs, matches);
        const CostMatrix flipped = transpose(costs);
        ASSERT_EQ(valueOf(flipped, hungarian.solve(flipped)), value) << "instance " << i;
        ASSERT_FALSE(better(valueOf(costs, GreedyNearestAllocator{}.solve(costs)), value));
        ASSERT_FALSE(better(valueOf(costs, CostGreedyAllocator{}.solve(costs)), value));
    }
}

// Regression: the potential of the virtual column 0 used to collect every
// delta of every phase. With thousands of rows that are mostly infeasible
// (each costing M ~ n * 2^41), it overflowed 64 bits (caught by UBSan). The
// instance is sparse, so every phase ends after a step or two and the test
// stays fast despite its size.
TEST(AllocatorPropertiesTest, ThousandsOfInfeasibleRowsDoNotOverflow) {
    constexpr std::size_t kSide = 4096;
    CostMatrix costs(kSide, kSide);
    costs.set(0, 0, TaskCost::of(-TaskCost::kLimit));
    costs.set(1, 1, TaskCost::of(TaskCost::kLimit));
    const auto matches = HungarianAllocator{}.solve(costs);
    ASSERT_EQ(assignmentProblem(costs, matches), "");
    EXPECT_EQ(matches, (std::vector<MatrixMatch>{{0, 0}, {1, 1}}));
}

}  // namespace
}  // namespace mapc
