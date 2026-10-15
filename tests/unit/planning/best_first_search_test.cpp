#include "mapc/planning/best_first_search.hpp"

#include "mapc/planning/heuristics.hpp"

#include "../support/graph_fixtures.hpp"
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

namespace mapc {
namespace {

using testing::buildOrThrow;
using testing::gridOrThrow;
using testing::pathProblem;

///   0 --1--> 1 --1--> 3
///   |                 ^
///   +--------5--------+      and 0 --1--> 2 --1--> 3 (via 2, cost 2 too)
///   4 is isolated; 3 -> 0 closes a cycle.
Graph diamond() {
    GraphBuilder builder;
    for (int i = 0; i < 5; ++i) {
        builder.addNode();
    }
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{1});
    builder.addEdge(NodeId{0}, NodeId{3}, Duration{5});
    builder.addEdge(NodeId{0}, NodeId{2}, Duration{1});
    builder.addEdge(NodeId{1}, NodeId{3}, Duration{1});
    builder.addEdge(NodeId{2}, NodeId{3}, Duration{1});
    builder.addEdge(NodeId{3}, NodeId{0}, Duration{1});
    return buildOrThrow(std::move(builder));
}

std::vector<NodeId> nodes(const Path& path) {
    std::vector<NodeId> out;
    out.reserve(path.steps.size());
    for (const PathStep& step : path.steps) {
        out.push_back(step.node);
    }
    return out;
}

PlanRequest request(NodeId start, NodeId goal) {
    PlanRequest r;
    r.agent = AgentId{0};
    r.start = start;
    r.goal = goal;
    return r;
}

// --- Basic behaviour -------------------------------------------------------------

TEST(DijkstraPlannerTest, FindsCheapestPath) {
    const Graph graph = diamond();
    const GraphView view(graph);
    DijkstraPlanner planner;
    const auto req = request(NodeId{0}, NodeId{3});
    const PlanResult result = planner.plan(view, req);
    ASSERT_TRUE(result.found());
    const Path& path = result.outcome.value();
    EXPECT_EQ(path.cost, Duration{2});
    EXPECT_EQ(pathProblem(view, req, path), "");
    // Tie between 0-1-3 and 0-2-3: broken deterministically (node 1 first,
    // because its edge comes first and ties prefer deeper/smaller ids).
    EXPECT_EQ(nodes(path), (std::vector<NodeId>{NodeId{0}, NodeId{1}, NodeId{3}}));
    EXPECT_GT(result.stats.expanded, 0U);
    EXPECT_GE(result.stats.generated, result.stats.expanded);
    EXPECT_GE(result.stats.max_open, 1U);
    EXPECT_EQ(planner.name(), "dijkstra");
}

TEST(DijkstraPlannerTest, StartEqualsGoalIsAZeroCostPath) {
    const Graph graph = diamond();
    DijkstraPlanner planner;
    PlanRequest req = request(NodeId{4}, NodeId{4});
    req.start_time = Tick{7};
    const PlanResult result = planner.plan(GraphView(graph), req);
    ASSERT_TRUE(result.found());
    EXPECT_EQ(result.outcome->steps, (std::vector<PathStep>{{NodeId{4}, Tick{7}}}));
    EXPECT_EQ(result.outcome->cost, Duration::zero());
    EXPECT_EQ(result.stats.expanded, 0U);
}

TEST(DijkstraPlannerTest, UnreachableGoalIsNoPath) {
    const Graph graph = diamond();
    DijkstraPlanner planner;
    const PlanResult result = planner.plan(GraphView(graph), request(NodeId{0}, NodeId{4}));
    ASSERT_FALSE(result.found());
    EXPECT_EQ(result.outcome.error(), PlanFailure::NoPath);
    EXPECT_EQ(toString(PlanFailure::NoPath), "no path");
}

TEST(DijkstraPlannerTest, EdgesAreDirected) {
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{1});
    const Graph graph = buildOrThrow(std::move(builder));
    DijkstraPlanner planner;
    EXPECT_TRUE(planner.plan(GraphView(graph), request(NodeId{0}, NodeId{1})).found());
    EXPECT_FALSE(planner.plan(GraphView(graph), request(NodeId{1}, NodeId{0})).found());
}

TEST(DijkstraPlannerTest, InvalidNodesAreInvalidRequests) {
    const Graph graph = diamond();
    DijkstraPlanner planner;
    for (const auto& [start, goal] : std::vector<std::pair<NodeId, NodeId>>{
             {NodeId{9}, NodeId{0}}, {NodeId{0}, NodeId{9}}, {NodeId::invalid(), NodeId{0}}}) {
        const PlanResult result = planner.plan(GraphView(graph), request(start, goal));
        ASSERT_FALSE(result.found());
        EXPECT_EQ(result.outcome.error(), PlanFailure::InvalidRequest);
    }
    EXPECT_EQ(toString(PlanFailure::InvalidRequest), "invalid request");
    EXPECT_EQ(toString(static_cast<PlanFailure>(77)), "unknown failure");
}

TEST(DijkstraPlannerTest, StartTimeOffsetsEveryStep) {
    const Graph graph = diamond();
    const GraphView view(graph);
    DijkstraPlanner planner;
    PlanRequest req = request(NodeId{0}, NodeId{3});
    req.start_time = Tick{100};
    const PlanResult result = planner.plan(view, req);
    ASSERT_TRUE(result.found());
    EXPECT_EQ(pathProblem(view, req, *result.outcome), "");
    EXPECT_EQ(result.outcome->steps.back().time, Tick{102});
}

// --- Overlays ------------------------------------------------------------------------

TEST(DijkstraPlannerTest, BlockedEdgesForceADetour) {
    const Graph graph = diamond();
    GraphOverlay overlay(graph);
    (void)overlay.block(*graph.findEdge(NodeId{1}, NodeId{3}));
    (void)overlay.block(*graph.findEdge(NodeId{2}, NodeId{3}));
    const GraphView view(graph, overlay);
    DijkstraPlanner planner;
    const auto req = request(NodeId{0}, NodeId{3});
    const PlanResult result = planner.plan(view, req);
    ASSERT_TRUE(result.found());
    EXPECT_EQ(nodes(*result.outcome), (std::vector<NodeId>{NodeId{0}, NodeId{3}}));
    EXPECT_EQ(result.outcome->cost, Duration{5});
    EXPECT_EQ(pathProblem(view, req, *result.outcome), "");

    (void)overlay.block(*graph.findEdge(NodeId{0}, NodeId{3}));
    EXPECT_EQ(planner.plan(GraphView(graph, overlay), req).outcome.error(), PlanFailure::NoPath);
}

TEST(DijkstraPlannerTest, RaisedCostsChangeTheRoute) {
    const Graph graph = diamond();
    GraphOverlay overlay(graph);
    (void)overlay.setCost(*graph.findEdge(NodeId{1}, NodeId{3}), Duration{10});
    const GraphView view(graph, overlay);
    DijkstraPlanner planner;
    const auto req = request(NodeId{0}, NodeId{3});
    const PlanResult result = planner.plan(view, req);
    ASSERT_TRUE(result.found());
    EXPECT_EQ(nodes(*result.outcome), (std::vector<NodeId>{NodeId{0}, NodeId{2}, NodeId{3}}));
    EXPECT_EQ(pathProblem(view, req, *result.outcome), "");
}

// --- Limits, reuse, determinism ------------------------------------------------------

TEST(BestFirstSearchTest, ExpansionLimitStopsTheSearch) {
    const GridMap map = gridOrThrow(GridMap::fromRows({"..........", "..........", ".........."}));
    const GraphView view(map.graph());
    DijkstraPlanner planner;
    PlanRequest req = request(*map.nodeAt(0, 0), *map.nodeAt(9, 2));
    req.limits.max_expansions = 3;
    const PlanResult limited = planner.plan(view, req);
    ASSERT_FALSE(limited.found());
    EXPECT_EQ(limited.outcome.error(), PlanFailure::LimitReached);
    EXPECT_EQ(limited.stats.expanded, 3U);
    EXPECT_EQ(toString(PlanFailure::LimitReached), "limit reached");

    req.limits.max_expansions = 1000;
    EXPECT_TRUE(planner.plan(view, req).found());
}

TEST(BestFirstSearchTest, PlannerCanBeReusedAcrossGraphsAndQueries) {
    const Graph small = diamond();
    const GridMap grid = gridOrThrow(GridMap::fromRows({"....", ".##.", "...."}));
    DijkstraPlanner reused;
    for (int round = 0; round < 3; ++round) {
        const PlanResult a = reused.plan(GraphView(small), request(NodeId{0}, NodeId{3}));
        const PlanResult b =
            reused.plan(GraphView(grid.graph()), request(*grid.nodeAt(0, 0), *grid.nodeAt(3, 2)));
        DijkstraPlanner fresh;
        EXPECT_EQ(a.outcome, fresh.plan(GraphView(small), request(NodeId{0}, NodeId{3})).outcome);
        EXPECT_EQ(
            b.outcome,
            fresh.plan(GraphView(grid.graph()), request(*grid.nodeAt(0, 0), *grid.nodeAt(3, 2)))
                .outcome);
    }
}

TEST(BestFirstSearchTest, ResultsAreDeterministicDespiteManyTies) {
    // An open grid has a huge number of equally short paths.
    const GridMap map = gridOrThrow(GridMap::fromRows(std::vector<std::string>(8, "........")));
    const auto heuristic = std::make_shared<GridHeuristic>(map);
    const auto req = request(*map.nodeAt(0, 0), *map.nodeAt(7, 7));
    AStarPlanner first(heuristic);
    AStarPlanner second(heuristic);
    const PlanResult a = first.plan(GraphView(map.graph()), req);
    const PlanResult b = second.plan(GraphView(map.graph()), req);
    const PlanResult c = first.plan(GraphView(map.graph()), req);
    ASSERT_TRUE(a.found());
    EXPECT_EQ(a.outcome, b.outcome);
    EXPECT_EQ(a.outcome, c.outcome);
    EXPECT_EQ(a.stats.expanded, b.stats.expanded);
    EXPECT_EQ(a.stats.generated, c.stats.generated);
    EXPECT_EQ(a.outcome->cost, Duration{14});
}

TEST(BestFirstSearchTest, SaturatesInsteadOfOverflowing) {
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration::max());
    builder.addEdge(NodeId{1}, NodeId{2}, Duration::max());
    const Graph graph = buildOrThrow(std::move(builder));
    DijkstraPlanner planner;
    const PlanResult result = planner.plan(GraphView(graph), request(NodeId{0}, NodeId{2}));
    ASSERT_TRUE(result.found());
    EXPECT_EQ(result.outcome->cost, Duration::max());
    EXPECT_EQ(result.outcome->steps.back().time, Tick::max());
}

// --- A* and weighted A* --------------------------------------------------------------

TEST(AStarPlannerTest, ExpandsFewerNodesThanDijkstraOnAnOpenGrid) {
    const GridMap map =
        gridOrThrow(GridMap::fromRows(std::vector<std::string>(30, std::string(30, '.'))));
    const auto req = request(*map.nodeAt(0, 0), *map.nodeAt(29, 0));
    DijkstraPlanner dijkstra;
    AStarPlanner astar(std::make_shared<GridHeuristic>(map));
    const PlanResult d = dijkstra.plan(GraphView(map.graph()), req);
    const PlanResult a = astar.plan(GraphView(map.graph()), req);
    ASSERT_TRUE(d.found());
    ASSERT_TRUE(a.found());
    EXPECT_EQ(d.outcome->cost, a.outcome->cost);
    EXPECT_LT(a.stats.expanded * 10, d.stats.expanded);
    EXPECT_EQ(astar.name(), "astar");
}

TEST(AStarPlannerTest, RejectsHeuristicOfAnotherGraph) {
    const GridMap map = gridOrThrow(GridMap::fromRows({"..."}));
    const GridMap other = gridOrThrow(GridMap::fromRows({"..."}));
    AStarPlanner planner(std::make_shared<GridHeuristic>(other));
    EXPECT_THROW((void)planner.plan(GraphView(map.graph()), request(NodeId{0}, NodeId{2})),
                 std::invalid_argument);
}

TEST(AStarPlannerTest, RejectsNullHeuristic) {
    EXPECT_THROW(AStarPlanner(nullptr), std::invalid_argument);
}

TEST(WeightedAStarPlannerTest, StaysWithinTheWeightBound) {
    const GridMap map =
        gridOrThrow(GridMap::fromRows({"..........", ".########.", ".#......#.", ".#.####.#.",
                                       "...#....#.", "####.####.", ".........."}));
    const auto heuristic = std::make_shared<GridHeuristic>(map);
    const auto req = request(*map.nodeAt(0, 0), *map.nodeAt(5, 4));
    DijkstraPlanner dijkstra;
    const Duration optimal = dijkstra.plan(GraphView(map.graph()), req).outcome->cost;
    for (const double w : {1.0, 1.5, 3.0, 10.0}) {
        const HeuristicWeight weight = HeuristicWeight::fromDouble(w);
        WeightedAStarPlanner planner(heuristic, weight);
        const PlanResult result = planner.plan(GraphView(map.graph()), req);
        ASSERT_TRUE(result.found()) << w;
        EXPECT_EQ(pathProblem(GraphView(map.graph()), req, *result.outcome), "");
        EXPECT_LE(result.outcome->cost.count() * weight.denominator(),
                  optimal.count() * weight.numerator())
            << "w=" << w;
        EXPECT_EQ(planner.weight(), weight);
        EXPECT_EQ(planner.name(), "wastar");
    }
}

TEST(WeightedAStarPlannerTest, PrioritiesDoNotSaturateForHugeCostsAndFineWeights) {
    // g * denominator exceeds 64 bits here. With saturating priorities both
    // frontier entries tie at the maximum and the expensive direct edge wins,
    // breaking the w * optimal bound.
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{9'000'000'000});
    builder.addEdge(NodeId{0}, NodeId{2}, Duration{5'000'000'000});
    builder.addEdge(NodeId{2}, NodeId{1}, Duration{1});
    const Graph graph = buildOrThrow(std::move(builder));
    const HeuristicWeight weight = HeuristicWeight::fraction(3'000'000'001U, 2'000'000'000U);
    WeightedAStarPlanner planner(std::make_shared<ZeroHeuristic>(), weight);
    const PlanResult result = planner.plan(GraphView(graph), request(NodeId{0}, NodeId{1}));
    ASSERT_TRUE(result.found());
    EXPECT_EQ(result.outcome->cost, Duration{5'000'000'001});
}

TEST(WeightedAStarPlannerTest, LargeHeuristicTimesLargeNumeratorIsExact) {
    // numerator * h also exceeds 64 bits: weight 1000 with estimates near 2^53.
    GraphBuilder builder;
    builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{1.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{2.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{1.0, 1.0}, 1});
    const Duration::rep big = Duration::rep{1} << 53U;
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{big});
    builder.addEdge(NodeId{1}, NodeId{2}, Duration{big});
    builder.addEdge(NodeId{0}, NodeId{3}, Duration{2 * big});
    builder.addEdge(NodeId{3}, NodeId{2}, Duration{2 * big});
    const Graph graph = buildOrThrow(std::move(builder));
    const auto euclidean = EuclideanHeuristic::create(graph);
    ASSERT_TRUE(euclidean.hasValue());
    WeightedAStarPlanner planner(std::make_shared<EuclideanHeuristic>(*euclidean),
                                 HeuristicWeight::fraction(1000, 1));
    const PlanResult result = planner.plan(GraphView(graph), request(NodeId{0}, NodeId{2}));
    ASSERT_TRUE(result.found());
    EXPECT_EQ(result.outcome->cost, Duration{2 * big});
}

// --- HeuristicWeight -------------------------------------------------------------------

TEST(HeuristicWeightTest, FractionsAreReduced) {
    const HeuristicWeight w = HeuristicWeight::fraction(6, 4);
    EXPECT_EQ(w.numerator(), 3U);
    EXPECT_EQ(w.denominator(), 2U);
    EXPECT_DOUBLE_EQ(w.value(), 1.5);
    EXPECT_EQ(HeuristicWeight::fraction(7, 7), HeuristicWeight::one());
    EXPECT_NE(HeuristicWeight::fraction(3, 2), HeuristicWeight::one());
}

TEST(HeuristicWeightTest, FromDoubleRoundsToThousandths) {
    EXPECT_EQ(HeuristicWeight::fromDouble(1.5), HeuristicWeight::fraction(3, 2));
    EXPECT_EQ(HeuristicWeight::fromDouble(1.0), HeuristicWeight::one());
    EXPECT_EQ(HeuristicWeight::fromDouble(1.0004), HeuristicWeight::one());
    EXPECT_EQ(HeuristicWeight::fromDouble(1000.0), HeuristicWeight::fraction(1000, 1));
    EXPECT_EQ(HeuristicWeight::fromDouble(2.0005).numerator(), 2001U);
}

TEST(HeuristicWeightTest, RejectsOutOfRangeWeights) {
    EXPECT_THROW((void)HeuristicWeight::fraction(0, 1), std::invalid_argument);
    EXPECT_THROW((void)HeuristicWeight::fraction(1, 0), std::invalid_argument);
    EXPECT_THROW((void)HeuristicWeight::fraction(1, 2), std::invalid_argument);
    EXPECT_THROW((void)HeuristicWeight::fraction(1001, 1), std::invalid_argument);
    EXPECT_THROW((void)HeuristicWeight::fromDouble(0.999), std::invalid_argument);
    EXPECT_THROW((void)HeuristicWeight::fromDouble(1000.5), std::invalid_argument);
    EXPECT_THROW((void)HeuristicWeight::fromDouble(std::nan("")), std::invalid_argument);
}

// --- Heuristics ----------------------------------------------------------------------

TEST(GridHeuristicTest, ManhattanForFourConnectivity) {
    GridOptions options;
    options.cardinal_cost = Duration{3};
    const GridMap map = gridOrThrow(GridMap::fromRows({"....", "....", "...."}, options));
    const GridHeuristic h(map);
    EXPECT_EQ(h.estimate(*map.nodeAt(0, 0), *map.nodeAt(3, 2)), Duration{15});
    EXPECT_EQ(h.estimate(*map.nodeAt(3, 2), *map.nodeAt(0, 0)), Duration{15});
    EXPECT_EQ(h.estimate(*map.nodeAt(1, 1), *map.nodeAt(1, 1)), Duration::zero());
    EXPECT_TRUE(h.compatibleWith(map.graph()));
    EXPECT_FALSE(h.compatibleWith(Graph{}));
    EXPECT_THROW((void)h.estimate(NodeId{99}, NodeId{0}), std::out_of_range);
    EXPECT_THROW((void)h.estimate(NodeId{0}, NodeId::invalid()), std::out_of_range);
}

TEST(GridHeuristicTest, OctileForEightConnectivity) {
    GridOptions options;
    options.connectivity = GridConnectivity::Eight;
    options.cardinal_cost = Duration{10};
    options.diagonal_cost = Duration{14};
    const GridMap map = gridOrThrow(GridMap::fromRows({"....", "....", "...."}, options));
    const GridHeuristic h(map);
    // dx = 3, dy = 2: two diagonal moves and one straight move.
    EXPECT_EQ(h.estimate(*map.nodeAt(0, 0), *map.nodeAt(3, 2)), Duration{38});
}

TEST(GridHeuristicTest, ExpensiveDiagonalsFallBackToTwoStraightMoves) {
    GridOptions options;
    options.connectivity = GridConnectivity::Eight;
    options.cardinal_cost = Duration{1};
    options.diagonal_cost = Duration{5};  // more than two orthogonal moves
    const GridMap map = gridOrThrow(GridMap::fromRows({"...", "..."}, options));
    const GridHeuristic h(map);
    EXPECT_EQ(h.estimate(*map.nodeAt(0, 0), *map.nodeAt(1, 1)), Duration{2});
}

TEST(GridHeuristicTest, CheapDiagonalsUseChebyshevDistance) {
    // With diagonals cheaper than straight moves, zig-zagging diagonally beats
    // a straight line: (0,0) -> (2,0) costs 2 diagonals = 6 rather than 2
    // straight moves = 10, so octile distance (10) would overestimate.
    GridOptions options;
    options.connectivity = GridConnectivity::Eight;
    options.cardinal_cost = Duration{5};
    options.diagonal_cost = Duration{3};
    const GridMap map = gridOrThrow(GridMap::fromRows({"...", "..."}, options));
    const GridHeuristic h(map);
    const auto req = request(*map.nodeAt(0, 0), *map.nodeAt(2, 0));
    DijkstraPlanner dijkstra;
    const Duration optimal = dijkstra.plan(GraphView(map.graph()), req).outcome->cost;
    EXPECT_EQ(optimal, Duration{6});
    EXPECT_LE(h.estimate(req.start, req.goal), optimal);
    EXPECT_EQ(h.estimate(req.start, req.goal), Duration{6});
}

TEST(EuclideanHeuristicTest, ScalesDistanceByTheCheapestCostPerLength) {
    GraphBuilder builder;
    builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{3.0, 4.0}, 1});   // distance 5
    builder.addNode(NodeAttributes{Point2{3.0, 14.0}, 1});  // 10 further
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{10});    // 2 per unit
    builder.addEdge(NodeId{1}, NodeId{2}, Duration{50});    // 5 per unit
    const Graph graph = buildOrThrow(std::move(builder));
    const auto h = EuclideanHeuristic::create(graph);
    ASSERT_TRUE(h.hasValue());
    EXPECT_NEAR(h->scale(), 2.0, 1e-6);
    EXPECT_EQ(h->estimate(NodeId{0}, NodeId{1}), Duration{9});  // floor(10 - margin)
    EXPECT_EQ(h->estimate(NodeId{2}, NodeId{2}), Duration::zero());
    EXPECT_TRUE(h->compatibleWith(graph));
    EXPECT_FALSE(h->compatibleWith(Graph{}));
    EXPECT_THROW((void)h->estimate(NodeId{3}, NodeId{0}), std::out_of_range);
}

TEST(EuclideanHeuristicTest, NeedsPositionsEverywhere) {
    GraphBuilder builder;
    builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
    builder.addNode();
    const Graph graph = buildOrThrow(std::move(builder));
    EXPECT_TRUE(EuclideanHeuristic::create(graph).hasError());
}

TEST(EuclideanHeuristicTest, DegenerateGraphsGiveZeroEstimates) {
    // No edges at all, and edges only between coincident nodes.
    GraphBuilder builder;
    builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{9.0, 9.0}, 1});
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{3});
    const Graph graph = buildOrThrow(std::move(builder));
    const auto h = EuclideanHeuristic::create(graph);
    ASSERT_TRUE(h.hasValue());
    EXPECT_EQ(h->scale(), 0.0);
    EXPECT_EQ(h->estimate(NodeId{0}, NodeId{2}), Duration::zero());
}

TEST(EuclideanHeuristicTest, HugeDistancesSaturate) {
    GraphBuilder builder;
    builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{1.0, 0.0}, 1});
    builder.addNode(NodeAttributes{Point2{1e300, 0.0}, 1});
    builder.addEdge(NodeId{0}, NodeId{1}, Duration::max());
    const Graph graph = buildOrThrow(std::move(builder));
    const auto h = EuclideanHeuristic::create(graph);
    ASSERT_TRUE(h.hasValue());
    EXPECT_EQ(h->estimate(NodeId{0}, NodeId{2}), Duration::max());
}

TEST(ZeroHeuristicTest, IsAlwaysZeroAndUniversal) {
    const ZeroHeuristic h;
    EXPECT_EQ(h.estimate(NodeId{1}, NodeId{2}), Duration::zero());
    EXPECT_TRUE(h.compatibleWith(Graph{}));
}

}  // namespace
}  // namespace mapc
