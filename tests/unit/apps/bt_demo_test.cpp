#include "toy_agent.hpp"
#include <gtest/gtest.h>

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mapc::apps {
namespace {

std::vector<TaskId> ids(std::initializer_list<TaskId::rep_type> values) {
    std::vector<TaskId> out;
    for (const TaskId::rep_type value : values) {
        out.emplace_back(value);
    }
    return out;
}

bool adjacentOrSame(GridCell a, GridCell b) {
    const auto dx = std::abs(static_cast<int>(a.x) - static_cast<int>(b.x));
    const auto dy = std::abs(static_cast<int>(a.y) - static_cast<int>(b.y));
    return dx + dy <= 1;
}

ToyRunResult runQuietly(const ToyScenario& scenario) {
    std::ostringstream out;
    return runToyScenario(scenario, out);
}

// --- The demo scenario ----------------------------------------------------------------

TEST(ToyAgentTest, DemoResolvesEveryTask) {
    const ToyRunResult result = runQuietly(demoScenario());
    ASSERT_TRUE(result.finished);
    const ToyAgentStats& s = result.stats;
    EXPECT_EQ(s.completed, ids({1, 3}));
    EXPECT_EQ(s.abandoned, ids({2}));  // sealed off: three failed plans
    EXPECT_EQ(s.cancelled, ids({4}));  // goal blocked
    EXPECT_EQ(s.plan_failures, 3U);
    EXPECT_EQ(s.path_invalidations, 1U);
    EXPECT_EQ(s.recoveries, 1U);
    // FollowPath was halted twice: by the blocked aisle and by the fault.
    EXPECT_EQ(s.follow_halts, 2U);
    EXPECT_EQ(result.positions.back(), (GridCell{8, 1}));
}

TEST(ToyAgentTest, DemoMovesOneCellPerTickAndNeverIntoABlockedCell) {
    const ToyRunResult result = runQuietly(demoScenario());
    GridCell previous{1, 1};
    for (std::size_t t = 0; t < result.positions.size(); ++t) {
        const GridCell cell = result.positions[t];
        EXPECT_TRUE(adjacentOrSame(previous, cell)) << "t=" << t;
        if (t >= 3 && t < 19) {
            EXPECT_NE(cell, (GridCell{8, 2})) << "t=" << t;  // blocked aisle
        }
        if (t >= 14) {
            EXPECT_NE(cell, (GridCell{4, 1})) << "t=" << t;
            EXPECT_NE(cell, (GridCell{4, 3})) << "t=" << t;
        }
        previous = cell;
    }
}

TEST(ToyAgentTest, TheFaultPreemptsTheTaskAndTheAgentStandsStill) {
    const ToyRunResult result = runQuietly(demoScenario());
    // Fault from t=9 until it clears at t=12: no movement on those ticks.
    ASSERT_GT(result.positions.size(), 13U);
    for (std::size_t t = 9; t <= 12; ++t) {
        EXPECT_EQ(result.positions[t], result.positions[8]) << "t=" << t;
    }
    EXPECT_NE(result.positions[13], result.positions[8]);  // replanned and moving again
}

TEST(ToyAgentTest, TheRunIsDeterministic) {
    std::ostringstream first;
    std::ostringstream second;
    (void)runToyScenario(demoScenario(), first, true);
    (void)runToyScenario(demoScenario(), second, true);
    EXPECT_EQ(first.str(), second.str());
    EXPECT_NE(first.str().find("abandon task#2"), std::string::npos);
}

// --- Smaller scenarios ----------------------------------------------------------------

ToyScenario corridor() {
    ToyScenario scenario;
    scenario.rows = {"#######", "#.....#", "#######"};
    scenario.start = {1, 1};
    return scenario;
}

TEST(ToyAgentTest, RetrySucceedsWhenTheWayOpensBetweenAttempts) {
    ToyScenario scenario = corridor();
    scenario.events = {
        {Tick{0}, "task", [](ToyWorld& w) { w.addTask({TaskId{7}, {5, 1}}); }},
        {Tick{0}, "block", [](ToyWorld& w) { w.setBlocked({3, 1}, true); }},
        // Attempt 1 fails at t=1, attempt 2 at t=2; the corridor opens for attempt 3.
        {Tick{3}, "unblock", [](ToyWorld& w) { w.setBlocked({3, 1}, false); }},
    };
    const ToyRunResult result = runQuietly(scenario);
    ASSERT_TRUE(result.finished);
    EXPECT_EQ(result.stats.completed, ids({7}));
    EXPECT_EQ(result.stats.plan_failures, 2U);
    EXPECT_TRUE(result.stats.abandoned.empty());
}

TEST(ToyAgentTest, StopsAtMaxTime) {
    ToyScenario scenario = corridor();
    scenario.events = {
        {Tick{0}, "task", [](ToyWorld& w) { w.addTask({TaskId{1}, {5, 1}}); }},
        {Tick{0}, "fault", [](ToyWorld& w) { w.setFaulted(true); }},
    };
    scenario.max_time = Tick{10};
    const ToyRunResult result = runQuietly(scenario);
    EXPECT_FALSE(result.finished);
    EXPECT_EQ(result.end, Tick{10});
    EXPECT_EQ(result.positions.size(), 11U);
    EXPECT_TRUE(result.stats.completed.empty());
}

TEST(ToyAgentTest, AFaultBeforeAnyTaskIsHandled) {
    ToyScenario scenario = corridor();
    scenario.events = {
        {Tick{0}, "fault", [](ToyWorld& w) { w.setFaulted(true); }},
        {Tick{2}, "fault cleared", [](ToyWorld& w) { w.setFaulted(false); }},
        {Tick{3}, "task", [](ToyWorld& w) { w.addTask({TaskId{1}, {2, 1}}); }},
    };
    const ToyRunResult result = runQuietly(scenario);
    ASSERT_TRUE(result.finished);
    EXPECT_EQ(result.stats.recoveries, 1U);
    EXPECT_EQ(result.stats.follow_halts, 0U);
    EXPECT_EQ(result.stats.completed, ids({1}));
}

// --- ToyWorld -------------------------------------------------------------------------

TEST(ToyWorldTest, ValidatesCells) {
    Result<GridMap, LoadError> map = GridMap::fromRows({"#.#", "#.#"});
    ASSERT_TRUE(map);
    EXPECT_THROW(ToyWorld(map.value(), {0, 0}), std::invalid_argument);
    ToyWorld world(std::move(map).value(), {1, 0});
    EXPECT_THROW(world.setBlocked({1, 0}, true), std::invalid_argument);  // the agent's cell
    EXPECT_THROW(world.setBlocked({0, 0}, true), std::invalid_argument);  // a wall
    EXPECT_THROW(world.addTask({TaskId{1}, {2, 1}}), std::invalid_argument);
}

TEST(ToyWorldTest, MovesOnlyAlongOpenEdgesFromTheAgent) {
    Result<GridMap, LoadError> map = GridMap::fromRows({"...", "###"});
    ASSERT_TRUE(map);
    ToyWorld world(std::move(map).value(), {0, 0});
    const Graph& graph = world.map().graph();
    const NodeId middle = *world.map().nodeAt(1, 0);
    const NodeId right = *world.map().nodeAt(2, 0);
    const EdgeId to_middle = *graph.findEdge(world.agentNode(), middle);
    const EdgeId middle_to_right = *graph.findEdge(middle, right);

    EXPECT_FALSE(world.move(middle_to_right));  // does not start at the agent
    world.setBlocked({1, 0}, true);
    EXPECT_TRUE(world.blocked({1, 0}));
    EXPECT_FALSE(world.move(to_middle));
    world.setBlocked({1, 0}, false);
    EXPECT_TRUE(world.move(to_middle));
    EXPECT_EQ(world.agentCell(), (GridCell{1, 0}));
}

// --- Command line -----------------------------------------------------------------------

TEST(BtDemoCliTest, RunsTheDemo) {
    std::ostringstream out;
    std::ostringstream err;
    EXPECT_EQ(runBtDemoCli({}, out, err), 0);
    EXPECT_NE(out.str().find("summary: finished"), std::string::npos);
    EXPECT_NE(out.str().find("ReactiveFallback \"agent\""), std::string::npos);
    EXPECT_TRUE(err.str().empty());
}

TEST(BtDemoCliTest, RejectsUnknownArguments) {
    std::ostringstream out;
    std::ostringstream err;
    EXPECT_EQ(runBtDemoCli({"--bogus"}, out, err), 2);
    EXPECT_NE(err.str().find("usage"), std::string::npos);
}

}  // namespace
}  // namespace mapc::apps
