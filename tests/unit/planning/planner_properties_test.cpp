// Property tests over many seeded random graphs. These implement the M1
// acceptance criteria: A* returns exactly the Dijkstra cost on 1000 random
// graphs, weighted A* stays within w times the optimum, and every heuristic is
// admissible and consistent.

#include "mapc/common/rng.hpp"
#include "mapc/planning/best_first_search.hpp"
#include "mapc/planning/heuristics.hpp"

#include "../support/graph_fixtures.hpp"
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mapc {
namespace {

using testing::distancesTo;
using testing::pathProblem;
using testing::randomCostGraph;
using testing::randomGeometricGraph;
using testing::randomGrid;

constexpr std::uint64_t kSeed = 20261015;
constexpr int kGraphs = 1000;

/// A random graph together with the strongest heuristic that applies to it.
struct Instance {
    std::string kind;
    std::shared_ptr<const Graph> graph;
    std::shared_ptr<const GridMap> grid;  // keeps grid-backed graphs alive
    std::shared_ptr<const Heuristic> heuristic;
};

Instance makeInstance(Rng& rng, int index) {
    switch (index % 4) {
        case 0: {
            auto graph = std::make_shared<const Graph>(
                randomGeometricGraph(rng, static_cast<std::size_t>(rng.uniformInt(2, 40)),
                                     static_cast<std::size_t>(rng.uniformInt(0, 160))));
            auto h = EuclideanHeuristic::create(*graph);
            return {"geometric", graph, nullptr,
                    std::make_shared<const EuclideanHeuristic>(std::move(h).value())};
        }
        case 1: {
            auto graph = std::make_shared<const Graph>(
                randomCostGraph(rng, static_cast<std::size_t>(rng.uniformInt(2, 40)),
                                static_cast<std::size_t>(rng.uniformInt(0, 160))));
            return {"cost-only", graph, nullptr, std::make_shared<const ZeroHeuristic>()};
        }
        default: {
            GridOptions options;
            if (index % 4 == 3) {
                options.connectivity = GridConnectivity::Eight;
                options.cardinal_cost = Duration{rng.uniformInt(1, 10)};
                options.diagonal_cost = Duration{rng.uniformInt(1, 25)};
            } else {
                options.cardinal_cost = Duration{rng.uniformInt(1, 5)};
            }
            auto grid = std::make_shared<const GridMap>(
                randomGrid(rng, static_cast<std::uint32_t>(rng.uniformInt(1, 12)),
                           static_cast<std::uint32_t>(rng.uniformInt(1, 12)),
                           static_cast<int>(rng.uniformInt(0, 40)), options));
            const std::shared_ptr<const Graph> graph(grid, &grid->graph());
            return {index % 4 == 3 ? "grid-8" : "grid-4", graph, grid,
                    std::make_shared<const GridHeuristic>(*grid)};
        }
    }
}

/// A random overlay that blocks ~10% of the edges and raises ~10% of costs.
GraphOverlay randomOverlay(Rng& rng, const Graph& graph) {
    GraphOverlay overlay(graph);
    for (std::size_t e = 0; e < graph.edgeCount(); ++e) {
        const EdgeId edge{static_cast<EdgeId::rep_type>(e)};
        const auto roll = rng.uniformInt(0, 9);
        if (roll == 0) {
            (void)overlay.block(edge);
        } else if (roll == 1) {
            (void)overlay.setCost(edge, graph.cost(edge) + Duration{rng.uniformInt(1, 30)});
        }
    }
    return overlay;
}

class PlannerPropertyTest : public ::testing::Test {
protected:
    static void forEachQuery(
        const std::function<void(const Instance&, const GraphView&, const PlanRequest&,
                                 const std::optional<Duration::rep>& optimal)>& check) {
        Rng rng(kSeed);
        int queries = 0;
        for (int i = 0; i < kGraphs; ++i) {
            // A fully blocked random grid has no nodes; draw again so every
            // one of the kGraphs iterations runs a real query.
            Instance instance = makeInstance(rng, i);
            while (instance.graph->nodeCount() == 0) {
                instance = makeInstance(rng, i);
            }
            const Graph& graph = *instance.graph;
            const bool with_overlay = rng.bernoulli(0.5);
            const GraphOverlay overlay =
                with_overlay ? randomOverlay(rng, graph) : GraphOverlay(graph);
            const GraphView view(graph, overlay);

            PlanRequest request;
            request.start =
                NodeId{static_cast<NodeId::rep_type>(rng.uniformBelow(graph.nodeCount()))};
            request.goal =
                NodeId{static_cast<NodeId::rep_type>(rng.uniformBelow(graph.nodeCount()))};
            request.start_time = Tick{rng.uniformInt(0, 100)};
            const auto reference = distancesTo(view, request.goal)[request.start.value()];
            check(instance, view, request, reference);
            ++queries;
        }
        ASSERT_EQ(queries, kGraphs);
    }
};

TEST_F(PlannerPropertyTest, DijkstraAndAStarMatchAnIndependentReference) {
    int found = 0;
    int unreachable = 0;
    forEachQuery([&](const Instance& instance, const GraphView& view, const PlanRequest& request,
                     const std::optional<Duration::rep>& optimal) {
        DijkstraPlanner dijkstra;
        AStarPlanner astar(instance.heuristic);
        const PlanResult d = dijkstra.plan(view, request);
        const PlanResult a = astar.plan(view, request);
        ASSERT_EQ(d.found(), optimal.has_value()) << instance.kind;
        ASSERT_EQ(a.found(), optimal.has_value()) << instance.kind;
        if (!optimal) {
            EXPECT_EQ(d.outcome.error(), PlanFailure::NoPath);
            EXPECT_EQ(a.outcome.error(), PlanFailure::NoPath);
            ++unreachable;
            return;
        }
        ++found;
        EXPECT_EQ(d.outcome->cost.count(), *optimal) << instance.kind;
        EXPECT_EQ(a.outcome->cost.count(), *optimal) << instance.kind;
        EXPECT_EQ(pathProblem(view, request, *d.outcome), "") << instance.kind;
        EXPECT_EQ(pathProblem(view, request, *a.outcome), "") << instance.kind;
        EXPECT_LE(a.stats.expanded, d.stats.expanded + 1) << instance.kind;
    });
    // The generator must exercise both outcomes substantially.
    EXPECT_GT(found, kGraphs / 4);
    EXPECT_GT(unreachable, kGraphs / 20);
}

TEST_F(PlannerPropertyTest, WeightedAStarStaysWithinItsBound) {
    const std::vector<HeuristicWeight> weights{
        HeuristicWeight::fraction(5, 4), HeuristicWeight::fraction(3, 2),
        HeuristicWeight::fraction(2, 1), HeuristicWeight::fraction(5, 1)};
    forEachQuery([&](const Instance& instance, const GraphView& view, const PlanRequest& request,
                     const std::optional<Duration::rep>& optimal) {
        for (const HeuristicWeight weight : weights) {
            WeightedAStarPlanner planner(instance.heuristic, weight);
            const PlanResult result = planner.plan(view, request);
            ASSERT_EQ(result.found(), optimal.has_value()) << instance.kind;
            if (!optimal) {
                continue;
            }
            EXPECT_EQ(pathProblem(view, request, *result.outcome), "") << instance.kind;
            // cost <= (numerator / denominator) * optimal, in exact integers.
            EXPECT_LE(result.outcome->cost.count() * weight.denominator(),
                      *optimal * weight.numerator())
                << instance.kind << " w=" << weight.value();
        }
    });
}

TEST_F(PlannerPropertyTest, HeuristicsAreAdmissibleAndConsistent) {
    Rng rng(kSeed + 1);
    for (int i = 0; i < kGraphs / 4; ++i) {
        const Instance instance = makeInstance(rng, i);
        const Graph& graph = *instance.graph;
        if (graph.nodeCount() == 0) {
            continue;
        }
        const GraphView view(graph);
        const NodeId goal{static_cast<NodeId::rep_type>(rng.uniformBelow(graph.nodeCount()))};
        const auto distance = distancesTo(view, goal);
        const Heuristic& h = *instance.heuristic;
        EXPECT_EQ(h.estimate(goal, goal), Duration::zero()) << instance.kind;
        for (std::size_t n = 0; n < graph.nodeCount(); ++n) {
            const NodeId node{static_cast<NodeId::rep_type>(n)};
            if (distance[n]) {
                EXPECT_LE(h.estimate(node, goal).count(), *distance[n]) << instance.kind;
            }
        }
        for (std::size_t e = 0; e < graph.edgeCount(); ++e) {
            const EdgeId edge{static_cast<EdgeId::rep_type>(e)};
            const Duration hu = h.estimate(graph.source(edge), goal);
            const Duration hv = h.estimate(graph.target(edge), goal);
            EXPECT_LE(hu, graph.cost(edge) + hv) << instance.kind;
        }
    }
}

}  // namespace
}  // namespace mapc
