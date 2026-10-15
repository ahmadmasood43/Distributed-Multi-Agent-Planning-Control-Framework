#include "mapc/graph/graph.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace mapc {

namespace {

std::uint64_t nextGraphId() noexcept {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

GraphError makeError(GraphErrorCode code, std::string message) {
    return GraphError{code, std::move(message), std::nullopt, std::nullopt};
}

GraphError nodeError(GraphErrorCode code, std::size_t index, std::string message) {
    GraphError error = makeError(code, std::move(message));
    error.node_index = index;
    return error;
}

GraphError edgeError(GraphErrorCode code, std::size_t index, std::string message) {
    GraphError error = makeError(code, std::move(message));
    error.edge_index = index;
    return error;
}

std::string edgeLabel(std::size_t index, NodeId from, NodeId to) {
    return "edge #" + std::to_string(index) + " (" + toString(from) + " -> " + toString(to) + ")";
}

}  // namespace

std::string_view toString(GraphErrorCode code) noexcept {
    switch (code) {
        case GraphErrorCode::UnknownNode:
            return "unknown node";
        case GraphErrorCode::SelfLoop:
            return "self loop";
        case GraphErrorCode::NonPositiveCost:
            return "non-positive cost";
        case GraphErrorCode::DuplicateEdge:
            return "duplicate edge";
        case GraphErrorCode::InvalidPosition:
            return "invalid position";
        case GraphErrorCode::ZeroCapacity:
            return "zero capacity";
        case GraphErrorCode::TooLarge:
            return "too large";
    }
    return "unknown error";
}

// --- Graph -------------------------------------------------------------------

Graph::Graph() : id_(nextGraphId()), offsets_(1, 0) {}

void Graph::requireNode(NodeId node) const {
    if (!contains(node)) {
        throw std::out_of_range("Graph: unknown " + toString(node));
    }
}

void Graph::requireEdge(EdgeId edge) const {
    if (!contains(edge)) {
        throw std::out_of_range("Graph: unknown " + toString(edge));
    }
}

Graph::EdgeRange Graph::outEdges(NodeId node) const {
    requireNode(node);
    return EdgeRange{offsets_[node.value()], offsets_[node.value() + 1]};
}

NodeId Graph::source(EdgeId edge) const {
    requireEdge(edge);
    return NodeId{sources_[edge.value()]};
}

NodeId Graph::target(EdgeId edge) const {
    requireEdge(edge);
    return NodeId{targets_[edge.value()]};
}

Duration Graph::cost(EdgeId edge) const {
    requireEdge(edge);
    return Duration{costs_[edge.value()]};
}

std::optional<EdgeId> Graph::findEdge(NodeId from, NodeId to) const {
    for (const EdgeId edge : outEdges(from)) {
        if (targets_[edge.value()] == to.value()) {
            return edge;
        }
    }
    return std::nullopt;
}

const NodeAttributes& Graph::attributes(NodeId node) const {
    requireNode(node);
    return attributes_[node.value()];
}

// --- GraphBuilder --------------------------------------------------------------

NodeId GraphBuilder::addNode(NodeAttributes attributes) {
    // Ids beyond the representable range are reported by build(); the id
    // returned here then wraps, but the graph can never be built.
    const auto id = static_cast<NodeId::rep_type>(attributes_.size());
    attributes_.push_back(attributes);
    return NodeId{id};
}

void GraphBuilder::addEdge(NodeId from, NodeId to, Duration cost) {
    edges_.push_back(PendingEdge{from, to, cost});
}

void GraphBuilder::addBidirectionalEdge(NodeId a, NodeId b, Duration cost) {
    addEdge(a, b, cost);
    addEdge(b, a, cost);
}

Result<Graph, GraphError> GraphBuilder::build() && {
    // Both ids reserve their largest value as the invalid sentinel.
    if (attributes_.size() >= NodeId::kInvalidValue) {
        return err(makeError(GraphErrorCode::TooLarge,
                             std::to_string(attributes_.size()) + " nodes exceed the id range"));
    }
    if (edges_.size() >= EdgeId::kInvalidValue) {
        return err(makeError(GraphErrorCode::TooLarge,
                             std::to_string(edges_.size()) + " edges exceed the id range"));
    }

    const std::size_t node_count = attributes_.size();
    bool all_positions = true;
    for (std::size_t i = 0; i < node_count; ++i) {
        const NodeAttributes& node = attributes_[i];
        const NodeId id{static_cast<NodeId::rep_type>(i)};
        if (node.position && !isFinite(*node.position)) {
            return err(nodeError(GraphErrorCode::InvalidPosition, i,
                                 toString(id) + " has a non-finite position"));
        }
        if (node.capacity == 0) {
            return err(
                nodeError(GraphErrorCode::ZeroCapacity, i, toString(id) + " has capacity 0"));
        }
        all_positions = all_positions && node.position.has_value();
    }

    // Validate edges in insertion order so the first reported error is the
    // first offending edge in the input.
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(edges_.size());
    auto min_cost = Duration::max();
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        const PendingEdge& edge = edges_[i];
        const bool from_ok = edge.from.valid() && edge.from.value() < node_count;
        const bool to_ok = edge.to.valid() && edge.to.value() < node_count;
        if (!from_ok || !to_ok) {
            return err(edgeError(GraphErrorCode::UnknownNode, i,
                                 edgeLabel(i, edge.from, edge.to) + " refers to an unknown node"));
        }
        if (edge.from == edge.to) {
            return err(edgeError(GraphErrorCode::SelfLoop, i,
                                 edgeLabel(i, edge.from, edge.to) + " is a self loop"));
        }
        if (edge.cost < Duration{1}) {
            return err(edgeError(GraphErrorCode::NonPositiveCost, i,
                                 edgeLabel(i, edge.from, edge.to) + " has cost " +
                                     std::to_string(edge.cost.count()) + " (must be >= 1)"));
        }
        const std::uint64_t key =
            (static_cast<std::uint64_t>(edge.from.value()) << 32U) | edge.to.value();
        if (!seen.insert(key).second) {
            return err(edgeError(GraphErrorCode::DuplicateEdge, i,
                                 edgeLabel(i, edge.from, edge.to) + " duplicates an earlier edge"));
        }
        min_cost = std::min(min_cost, edge.cost);
    }

    Graph graph;
    graph.attributes_ = std::move(attributes_);
    graph.all_positions_ = all_positions;
    graph.min_cost_ = edges_.empty() ? Duration::zero() : min_cost;

    // Stable counting sort by source node: edges of a node keep their
    // insertion order.
    graph.offsets_.assign(node_count + 1, 0);
    for (const PendingEdge& edge : edges_) {
        ++graph.offsets_[edge.from.value() + 1];
    }
    for (std::size_t i = 0; i < node_count; ++i) {
        graph.offsets_[i + 1] += graph.offsets_[i];
    }
    graph.sources_.resize(edges_.size());
    graph.targets_.resize(edges_.size());
    graph.costs_.resize(edges_.size());
    std::vector<EdgeId::rep_type> cursor(graph.offsets_.begin(), graph.offsets_.end() - 1);
    for (const PendingEdge& edge : edges_) {
        const EdgeId::rep_type slot = cursor[edge.from.value()]++;
        graph.sources_[slot] = edge.from.value();
        graph.targets_[slot] = edge.to.value();
        graph.costs_[slot] = edge.cost.count();
    }

    edges_.clear();
    return graph;
}

}  // namespace mapc
