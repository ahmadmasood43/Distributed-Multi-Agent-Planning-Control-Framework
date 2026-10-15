#include "mapc/allocation/distance_oracle.hpp"

#include "mapc/common/rng.hpp"

#include "../support/graph_fixtures.hpp"
#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>

namespace mapc {
namespace {

using testing::buildOrThrow;
using testing::distancesTo;
using testing::randomCostGraph;

/// 0 -> 1 -> 2 (costs 2, 3), 0 -> 2 (cost 10), and an isolated node 3.
Graph lineGraph() {
    GraphBuilder builder;
    for (int i = 0; i < 4; ++i) {
        builder.addNode();
    }
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{2});
    builder.addEdge(NodeId{1}, NodeId{2}, Duration{3});
    builder.addEdge(NodeId{0}, NodeId{2}, Duration{10});
    return buildOrThrow(std::move(builder));
}

TEST(DistanceOracleTest, ShortestDistancesAndUnreachable) {
    const Graph graph = lineGraph();
    DistanceOracle oracle{GraphView(graph)};
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{0}), Duration{0});
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{1}), Duration{2});
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{2}), Duration{5});
    EXPECT_EQ(oracle.distance(NodeId{2}, NodeId{0}), std::nullopt);  // edges are directed
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{3}), std::nullopt);
}

TEST(DistanceOracleTest, CachesOneSearchPerSource) {
    const Graph graph = lineGraph();
    DistanceOracle oracle{GraphView(graph)};
    EXPECT_EQ(oracle.cachedSources(), 0U);
    (void)oracle.distance(NodeId{0}, NodeId{1});
    (void)oracle.distance(NodeId{0}, NodeId{2});
    (void)oracle.distance(NodeId{0}, NodeId{3});
    EXPECT_EQ(oracle.searches(), 1U);
    (void)oracle.distance(NodeId{1}, NodeId{2});
    EXPECT_EQ(oracle.searches(), 2U);
    EXPECT_EQ(oracle.cachedSources(), 2U);

    oracle.clear();
    EXPECT_EQ(oracle.cachedSources(), 0U);
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{2}), Duration{5});
    EXPECT_EQ(oracle.searches(), 3U);
}

TEST(DistanceOracleTest, UnknownNodesThrow) {
    const Graph graph = lineGraph();
    DistanceOracle oracle{GraphView(graph)};
    EXPECT_THROW((void)oracle.distance(NodeId{4}, NodeId{0}), std::out_of_range);
    EXPECT_THROW((void)oracle.distance(NodeId{0}, NodeId{4}), std::out_of_range);
    EXPECT_THROW((void)oracle.distance(NodeId::invalid(), NodeId{0}), std::out_of_range);
    EXPECT_EQ(oracle.searches(), 0U);
}

TEST(DistanceOracleTest, HonoursOverlayAndDropsCacheWhenItChanges) {
    const Graph graph = lineGraph();
    GraphOverlay overlay(graph);
    DistanceOracle oracle{GraphView(graph, overlay)};
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{2}), Duration{5});

    const EdgeId middle = *graph.findEdge(NodeId{1}, NodeId{2});
    ASSERT_TRUE(overlay.setCost(middle, Duration{20}));
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{2}), Duration{10});

    ASSERT_TRUE(overlay.block(*graph.findEdge(NodeId{0}, NodeId{2})));
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{2}), Duration{22});

    ASSERT_TRUE(overlay.block(middle));
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{2}), std::nullopt);

    overlay.clear();
    EXPECT_EQ(oracle.distance(NodeId{0}, NodeId{2}), Duration{5});
    EXPECT_EQ(oracle.cachedSources(), 1U);
}

// The oracle against an independent Bellman-Ford reference on random graphs,
// with and without random overlays.
TEST(DistanceOracleTest, MatchesBellmanFordOnRandomGraphs) {
    Rng rng(20261016);
    for (int round = 0; round < 200; ++round) {
        const Graph graph = randomCostGraph(rng, static_cast<std::size_t>(rng.uniformInt(1, 30)),
                                            static_cast<std::size_t>(rng.uniformInt(0, 120)));
        GraphOverlay overlay(graph);
        if (round % 2 == 1) {
            for (std::size_t e = 0; e < graph.edgeCount(); ++e) {
                const EdgeId edge{static_cast<EdgeId::rep_type>(e)};
                const auto roll = rng.uniformInt(0, 9);
                if (roll == 0) {
                    ASSERT_TRUE(overlay.block(edge));
                } else if (roll == 1) {
                    ASSERT_TRUE(overlay.setCost(edge, graph.cost(edge) + Duration{7}));
                }
            }
        }
        const GraphView view(graph, overlay);
        DistanceOracle oracle(view);
        for (std::size_t to = 0; to < graph.nodeCount(); ++to) {
            const NodeId goal{static_cast<NodeId::rep_type>(to)};
            const auto reference = distancesTo(view, goal);
            for (std::size_t from = 0; from < graph.nodeCount(); ++from) {
                const auto got = oracle.distance(NodeId{static_cast<NodeId::rep_type>(from)}, goal);
                ASSERT_EQ(got.has_value(), reference[from].has_value())
                    << "round " << round << ", " << from << " -> " << to;
                if (got) {
                    ASSERT_EQ(got->count(), *reference[from]) << "round " << round;
                }
            }
        }
        EXPECT_LE(oracle.searches(), graph.nodeCount());
    }
}

}  // namespace
}  // namespace mapc
