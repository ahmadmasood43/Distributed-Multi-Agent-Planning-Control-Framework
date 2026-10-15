#include "mapc/graph/graph.hpp"

#include "mapc/graph/graph_overlay.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace mapc {
namespace {

Graph buildOrDie(GraphBuilder builder) {
    auto graph = std::move(builder).build();
    if (!graph) {
        throw std::runtime_error(graph.error().message);
    }
    return std::move(graph).value();
}

std::vector<NodeId> targets(const Graph& graph, NodeId node) {
    std::vector<NodeId> out;
    for (const EdgeId edge : graph.outEdges(node)) {
        out.push_back(graph.target(edge));
    }
    return out;
}

// --- Geometry ----------------------------------------------------------------------

TEST(GeometryTest, PointComparisonPrintingAndDistance) {
    EXPECT_EQ((Point2{1.0, 2.0}), (Point2{1.0, 2.0}));
    EXPECT_NE((Point2{1.0, 2.0}), (Point2{2.0, 1.0}));
    std::ostringstream os;
    os << Point2{1.5, -2.0};
    EXPECT_EQ(os.str(), "(1.5, -2)");
    EXPECT_DOUBLE_EQ(distance(Point2{0.0, 0.0}, Point2{3.0, 4.0}), 5.0);
    EXPECT_TRUE(isFinite(Point2{1.0, 1.0}));
    EXPECT_FALSE(isFinite(Point2{std::numeric_limits<double>::infinity(), 1.0}));
}

// --- Construction ----------------------------------------------------------------

TEST(GraphTest, DefaultGraphIsEmpty) {
    const Graph graph;
    EXPECT_EQ(graph.nodeCount(), 0U);
    EXPECT_EQ(graph.edgeCount(), 0U);
    EXPECT_TRUE(graph.hasAllPositions());
    EXPECT_EQ(graph.minEdgeCost(), Duration::zero());
    EXPECT_FALSE(graph.contains(NodeId{0}));
    EXPECT_FALSE(graph.contains(EdgeId{0}));
}

TEST(GraphTest, NodesWithoutEdges) {
    GraphBuilder builder;
    const NodeId a = builder.addNode();
    const NodeId b = builder.addNode();
    EXPECT_EQ(a, NodeId{0});
    EXPECT_EQ(b, NodeId{1});
    const Graph graph = buildOrDie(std::move(builder));
    EXPECT_EQ(graph.nodeCount(), 2U);
    EXPECT_TRUE(graph.outEdges(a).empty());
    EXPECT_EQ(graph.outEdges(b).size(), 0U);
    EXPECT_FALSE(graph.hasAllPositions());
}

TEST(GraphTest, CsrKeepsInsertionOrderPerSource) {
    GraphBuilder builder;
    for (int i = 0; i < 4; ++i) {
        builder.addNode();
    }
    // Interleave sources to exercise the counting sort.
    builder.addEdge(NodeId{2}, NodeId{0}, Duration{5});
    builder.addEdge(NodeId{0}, NodeId{3}, Duration{1});
    builder.addEdge(NodeId{2}, NodeId{1}, Duration{7});
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{2});
    builder.addEdge(NodeId{3}, NodeId{2}, Duration{3});
    const Graph graph = buildOrDie(std::move(builder));

    EXPECT_EQ(graph.edgeCount(), 5U);
    EXPECT_EQ(targets(graph, NodeId{0}), (std::vector<NodeId>{NodeId{3}, NodeId{1}}));
    EXPECT_TRUE(targets(graph, NodeId{1}).empty());
    EXPECT_EQ(targets(graph, NodeId{2}), (std::vector<NodeId>{NodeId{0}, NodeId{1}}));
    EXPECT_EQ(targets(graph, NodeId{3}), (std::vector<NodeId>{NodeId{2}}));

    // Edge ids are contiguous per node and every accessor agrees.
    std::size_t visited = 0;
    for (std::size_t n = 0; n < graph.nodeCount(); ++n) {
        const NodeId node{static_cast<NodeId::rep_type>(n)};
        for (const EdgeId edge : graph.outEdges(node)) {
            EXPECT_EQ(graph.source(edge), node);
            ++visited;
        }
    }
    EXPECT_EQ(visited, graph.edgeCount());
    EXPECT_EQ(graph.cost(*graph.findEdge(NodeId{2}, NodeId{1})), Duration{7});
    EXPECT_EQ(graph.minEdgeCost(), Duration{1});
}

TEST(GraphTest, EdgeRangeIteratorBehavesLikeAForwardIterator) {
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{1});
    builder.addEdge(NodeId{0}, NodeId{2}, Duration{1});
    const Graph graph = buildOrDie(std::move(builder));
    const auto range = graph.outEdges(NodeId{0});
    auto it = range.begin();
    EXPECT_EQ(*it, EdgeId{0});
    auto old = it++;
    EXPECT_EQ(*old, EdgeId{0});
    EXPECT_EQ(*it, EdgeId{1});
    EXPECT_NE(it, range.end());
    ++it;
    EXPECT_EQ(it, range.end());
    static_assert(
        std::is_same_v<std::iterator_traits<Graph::EdgeRange::Iterator>::iterator_category,
                       std::forward_iterator_tag>);
}

TEST(GraphTest, BidirectionalEdgesAddBothDirections) {
    GraphBuilder builder;
    const NodeId a = builder.addNode();
    const NodeId b = builder.addNode();
    builder.addBidirectionalEdge(a, b, Duration{4});
    EXPECT_EQ(builder.edgeCount(), 2U);
    const Graph graph = buildOrDie(std::move(builder));
    ASSERT_TRUE(graph.findEdge(a, b).has_value());
    ASSERT_TRUE(graph.findEdge(b, a).has_value());
    EXPECT_EQ(graph.cost(*graph.findEdge(b, a)), Duration{4});
}

TEST(GraphTest, FindEdgeReturnsNulloptForMissingEdge) {
    GraphBuilder builder;
    const NodeId a = builder.addNode();
    const NodeId b = builder.addNode();
    builder.addEdge(a, b, Duration{1});
    const Graph graph = buildOrDie(std::move(builder));
    EXPECT_FALSE(graph.findEdge(b, a).has_value());
    EXPECT_THROW((void)graph.findEdge(NodeId{9}, a), std::out_of_range);
}

TEST(GraphTest, AttributesArePreserved) {
    GraphBuilder builder;
    builder.addNode(NodeAttributes{Point2{1.5, -2.0}, 3});
    builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
    const Graph graph = buildOrDie(std::move(builder));
    EXPECT_TRUE(graph.hasAllPositions());
    EXPECT_EQ(graph.attributes(NodeId{0}).position, (Point2{1.5, -2.0}));
    EXPECT_EQ(graph.attributes(NodeId{0}).capacity, 3U);
}

TEST(GraphTest, AccessorsRejectUnknownIds) {
    const Graph graph;
    EXPECT_THROW((void)graph.outEdges(NodeId{0}), std::out_of_range);
    EXPECT_THROW((void)graph.outEdges(NodeId::invalid()), std::out_of_range);
    EXPECT_THROW((void)graph.source(EdgeId{0}), std::out_of_range);
    EXPECT_THROW((void)graph.target(EdgeId::invalid()), std::out_of_range);
    EXPECT_THROW((void)graph.cost(EdgeId{0}), std::out_of_range);
    EXPECT_THROW((void)graph.attributes(NodeId{0}), std::out_of_range);
}

TEST(GraphTest, EveryBuiltGraphHasADistinctId) {
    const Graph a;
    const Graph b;
    EXPECT_NE(a.id(), b.id());
    // A copy is the same graph, so it keeps the id.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const Graph copy = a;
    EXPECT_EQ(copy.id(), a.id());
}

// --- Validation ------------------------------------------------------------------

GraphError buildError(GraphBuilder builder) {
    auto graph = std::move(builder).build();
    EXPECT_FALSE(graph.hasValue());
    return graph.hasError() ? graph.error() : GraphError{};
}

TEST(GraphBuilderTest, RejectsUnknownNodes) {
    GraphBuilder builder;
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{1});
    const GraphError error = buildError(std::move(builder));
    EXPECT_EQ(error.code, GraphErrorCode::UnknownNode);
    EXPECT_EQ(error.edge_index, 0U);
    EXPECT_NE(error.message.find("edge #0"), std::string::npos);
}

TEST(GraphBuilderTest, RejectsInvalidNodeId) {
    GraphBuilder builder;
    builder.addNode();
    builder.addEdge(NodeId::invalid(), NodeId{0}, Duration{1});
    EXPECT_EQ(buildError(std::move(builder)).code, GraphErrorCode::UnknownNode);
}

TEST(GraphBuilderTest, RejectsSelfLoops) {
    GraphBuilder builder;
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{0}, Duration{1});
    EXPECT_EQ(buildError(std::move(builder)).code, GraphErrorCode::SelfLoop);
}

TEST(GraphBuilderTest, RejectsNonPositiveCosts) {
    for (const auto cost : {Duration{0}, Duration{-3}, Duration::min()}) {
        GraphBuilder builder;
        builder.addNode();
        builder.addNode();
        builder.addEdge(NodeId{0}, NodeId{1}, cost);
        EXPECT_EQ(buildError(std::move(builder)).code, GraphErrorCode::NonPositiveCost);
    }
}

TEST(GraphBuilderTest, AcceptsVeryLargeCosts) {
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration::max());
    const Graph graph = buildOrDie(std::move(builder));
    EXPECT_EQ(graph.cost(EdgeId{0}), Duration::max());
}

TEST(GraphBuilderTest, RejectsDuplicateEdgesButNotReverseEdges) {
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{1});
    builder.addEdge(NodeId{1}, NodeId{0}, Duration{1});
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{2});
    const GraphError error = buildError(std::move(builder));
    EXPECT_EQ(error.code, GraphErrorCode::DuplicateEdge);
    EXPECT_EQ(error.edge_index, 2U);
}

TEST(GraphBuilderTest, ReportsTheFirstErrorInInsertionOrder) {
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{0});  // first problem
    builder.addEdge(NodeId{1}, NodeId{1}, Duration{1});  // second problem
    const GraphError error = buildError(std::move(builder));
    EXPECT_EQ(error.code, GraphErrorCode::NonPositiveCost);
    EXPECT_EQ(error.edge_index, 0U);
}

TEST(GraphBuilderTest, RejectsNonFinitePositions) {
    for (const double bad :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        GraphBuilder builder;
        builder.addNode(NodeAttributes{Point2{0.0, 0.0}, 1});
        builder.addNode(NodeAttributes{Point2{bad, 0.0}, 1});
        const GraphError error = buildError(std::move(builder));
        EXPECT_EQ(error.code, GraphErrorCode::InvalidPosition);
        EXPECT_EQ(error.node_index, 1U);
    }
}

TEST(GraphBuilderTest, RejectsZeroCapacity) {
    GraphBuilder builder;
    builder.addNode(NodeAttributes{std::nullopt, 0});
    EXPECT_EQ(buildError(std::move(builder)).code, GraphErrorCode::ZeroCapacity);
}

TEST(GraphBuilderTest, ErrorCodesHaveNames) {
    for (const auto code :
         {GraphErrorCode::UnknownNode, GraphErrorCode::SelfLoop, GraphErrorCode::NonPositiveCost,
          GraphErrorCode::DuplicateEdge, GraphErrorCode::InvalidPosition,
          GraphErrorCode::ZeroCapacity, GraphErrorCode::TooLarge}) {
        EXPECT_FALSE(toString(code).empty());
        EXPECT_NE(toString(code), "unknown error");
    }
    EXPECT_EQ(toString(static_cast<GraphErrorCode>(99)), "unknown error");
}

// --- Overlay and view --------------------------------------------------------------

Graph chain() {
    GraphBuilder builder;
    builder.addNode();
    builder.addNode();
    builder.addNode();
    builder.addEdge(NodeId{0}, NodeId{1}, Duration{2});
    builder.addEdge(NodeId{1}, NodeId{2}, Duration{3});
    return buildOrDie(std::move(builder));
}

TEST(GraphOverlayTest, StartsAsTheBaseGraph) {
    const Graph graph = chain();
    const GraphOverlay overlay(graph);
    EXPECT_EQ(overlay.graphId(), graph.id());
    EXPECT_EQ(overlay.edgeCount(), 2U);
    EXPECT_EQ(overlay.version(), 0U);
    EXPECT_FALSE(overlay.isBlocked(EdgeId{0}));
    EXPECT_EQ(overlay.cost(EdgeId{1}), Duration{3});
    EXPECT_EQ(overlay.blockedCount(), 0U);
}

TEST(GraphOverlayTest, BlockAndUnblockAreIdempotentAndVersioned) {
    const Graph graph = chain();
    GraphOverlay overlay(graph);
    EXPECT_TRUE(overlay.block(EdgeId{0}).hasValue());
    EXPECT_EQ(overlay.version(), 1U);
    EXPECT_TRUE(overlay.block(EdgeId{0}).hasValue());  // no change, no new version
    EXPECT_EQ(overlay.version(), 1U);
    EXPECT_TRUE(overlay.isBlocked(EdgeId{0}));
    EXPECT_EQ(overlay.blockedCount(), 1U);
    EXPECT_TRUE(overlay.unblock(EdgeId{0}).hasValue());
    EXPECT_TRUE(overlay.unblock(EdgeId{0}).hasValue());
    EXPECT_EQ(overlay.version(), 2U);
    EXPECT_EQ(overlay.blockedCount(), 0U);
}

TEST(GraphOverlayTest, CostsCanOnlyBeRaised) {
    const Graph graph = chain();
    GraphOverlay overlay(graph);
    EXPECT_TRUE(overlay.setCost(EdgeId{0}, Duration{10}).hasValue());
    EXPECT_EQ(overlay.cost(EdgeId{0}), Duration{10});
    // Lowering below the base cost would make heuristics inadmissible.
    const auto lowered = overlay.setCost(EdgeId{0}, Duration{1});
    ASSERT_TRUE(lowered.hasError());
    EXPECT_EQ(lowered.error(), OverlayError::CostBelowBase);
    EXPECT_EQ(overlay.cost(EdgeId{0}), Duration{10});
    // Back to the base cost is allowed, as is resetting.
    EXPECT_TRUE(overlay.setCost(EdgeId{0}, Duration{2}).hasValue());
    EXPECT_TRUE(overlay.setCost(EdgeId{1}, Duration{8}).hasValue());
    EXPECT_TRUE(overlay.resetCost(EdgeId{1}).hasValue());
    EXPECT_EQ(overlay.cost(EdgeId{1}), Duration{3});
}

TEST(GraphOverlayTest, UnknownEdgesAreRejected) {
    const Graph graph = chain();
    GraphOverlay overlay(graph);
    for (const EdgeId edge : {EdgeId{2}, EdgeId::invalid()}) {
        EXPECT_EQ(overlay.block(edge).error(), OverlayError::UnknownEdge);
        EXPECT_EQ(overlay.unblock(edge).error(), OverlayError::UnknownEdge);
        EXPECT_EQ(overlay.setCost(edge, Duration{5}).error(), OverlayError::UnknownEdge);
        EXPECT_EQ(overlay.resetCost(edge).error(), OverlayError::UnknownEdge);
        EXPECT_THROW((void)overlay.isBlocked(edge), std::out_of_range);
        EXPECT_THROW((void)overlay.cost(edge), std::out_of_range);
    }
    EXPECT_EQ(overlay.version(), 0U);
    EXPECT_EQ(toString(OverlayError::UnknownEdge), "unknown edge");
    EXPECT_EQ(toString(OverlayError::CostBelowBase), "cost below base cost");
    EXPECT_EQ(toString(static_cast<OverlayError>(42)), "unknown error");
}

TEST(GraphOverlayTest, ClearRestoresTheBaseGraph) {
    const Graph graph = chain();
    GraphOverlay overlay(graph);
    overlay.clear();
    EXPECT_EQ(overlay.version(), 0U) << "clearing an unchanged overlay is not a change";
    (void)overlay.block(EdgeId{1});
    (void)overlay.setCost(EdgeId{0}, Duration{9});
    overlay.clear();
    EXPECT_EQ(overlay.version(), 3U);
    EXPECT_FALSE(overlay.isBlocked(EdgeId{1}));
    EXPECT_EQ(overlay.cost(EdgeId{0}), Duration{2});
    EXPECT_EQ(overlay.blockedCount(), 0U);
}

TEST(GraphViewTest, WithoutOverlayUsesBaseCosts) {
    const Graph graph = chain();
    const GraphView view(graph);
    EXPECT_EQ(&view.graph(), &graph);
    EXPECT_EQ(view.overlay(), nullptr);
    EXPECT_TRUE(view.traversable(EdgeId{0}));
    EXPECT_EQ(view.cost(EdgeId{1}), Duration{3});
}

TEST(GraphViewTest, WithOverlayUsesEffectiveCosts) {
    const Graph graph = chain();
    GraphOverlay overlay(graph);
    (void)overlay.block(EdgeId{0});
    (void)overlay.setCost(EdgeId{1}, Duration{30});
    const GraphView view(graph, overlay);
    EXPECT_FALSE(view.traversable(EdgeId{0}));
    EXPECT_EQ(view.cost(EdgeId{1}), Duration{30});
}

TEST(GraphViewTest, RejectsOverlayOfAnotherGraph) {
    const Graph graph = chain();
    const Graph other = chain();  // same shape, different graph
    const GraphOverlay overlay(other);
    EXPECT_THROW(GraphView(graph, overlay), std::invalid_argument);
}

}  // namespace
}  // namespace mapc
