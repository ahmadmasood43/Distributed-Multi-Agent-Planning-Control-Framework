#pragma once

#include "mapc/common/result.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mapc {

/// Per-node data stored in a Graph.
struct NodeAttributes {
    /// Position in the plane; required by distance heuristics.
    std::optional<Point2> position;
    /// How many agents the node can hold at once (enforced from M5 on).
    std::uint32_t capacity = 1;
};

enum class GraphErrorCode : std::uint8_t {
    UnknownNode,      ///< an edge refers to a node that was never added
    SelfLoop,         ///< an edge from a node to itself (waiting is not an edge)
    NonPositiveCost,  ///< traversal costs must be at least one tick
    DuplicateEdge,    ///< two edges with the same source and target
    InvalidPosition,  ///< a node position is NaN or infinite
    ZeroCapacity,     ///< a node must be able to hold at least one agent
    TooLarge,         ///< more nodes or edges than an id can represent
};

[[nodiscard]] std::string_view toString(GraphErrorCode code) noexcept;

struct GraphError {
    GraphErrorCode code = GraphErrorCode::UnknownNode;
    std::string message;
    /// Insertion index (0-based) of the offending node or edge, if the error
    /// concerns one, so loaders can map it back to a line of their input.
    std::optional<std::size_t> node_index;
    std::optional<std::size_t> edge_index;
};

class GraphBuilder;

/// An immutable, directed, weighted graph in compressed sparse row (CSR)
/// layout.
///
/// Nodes are numbered 0..nodeCount()-1. The outgoing edges of a node have
/// consecutive EdgeIds, ordered by the order in which they were added, so
/// iteration order (and therefore search tie-breaking) is deterministic.
/// Traversal costs are whole ticks (at least one).
///
/// Dynamic changes (blocked edges, raised costs) never modify a Graph; they
/// live in a GraphOverlay, so one Graph can be shared by many threads without
/// locking. Every built graph gets a process-unique id() that overlays and
/// heuristics use to detect being paired with the wrong graph.
class Graph {
public:
    /// A contiguous range of EdgeIds, iterable with range-for.
    class EdgeRange {
    public:
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = EdgeId;
            using difference_type = std::ptrdiff_t;
            using pointer = const EdgeId*;
            using reference = EdgeId;

            constexpr Iterator() noexcept = default;
            constexpr explicit Iterator(EdgeId::rep_type value) noexcept : value_(value) {}

            constexpr EdgeId operator*() const noexcept { return EdgeId{value_}; }
            constexpr Iterator& operator++() noexcept {
                ++value_;
                return *this;
            }
            constexpr Iterator operator++(int) noexcept {
                Iterator previous = *this;
                ++value_;
                return previous;
            }
            friend constexpr bool operator==(Iterator a, Iterator b) noexcept {
                return a.value_ == b.value_;
            }
            friend constexpr bool operator!=(Iterator a, Iterator b) noexcept {
                return a.value_ != b.value_;
            }

        private:
            EdgeId::rep_type value_ = 0;
        };

        constexpr EdgeRange(EdgeId::rep_type first, EdgeId::rep_type last) noexcept
            : first_(first), last_(last) {}

        [[nodiscard]] constexpr Iterator begin() const noexcept { return Iterator{first_}; }
        [[nodiscard]] constexpr Iterator end() const noexcept { return Iterator{last_}; }
        [[nodiscard]] constexpr std::size_t size() const noexcept { return last_ - first_; }
        [[nodiscard]] constexpr bool empty() const noexcept { return first_ == last_; }

    private:
        EdgeId::rep_type first_;
        EdgeId::rep_type last_;
    };

    /// An empty graph (no nodes, no edges).
    Graph();

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::size_t nodeCount() const noexcept { return attributes_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return targets_.size(); }

    [[nodiscard]] bool contains(NodeId node) const noexcept {
        return node.valid() && node.value() < nodeCount();
    }
    [[nodiscard]] bool contains(EdgeId edge) const noexcept {
        return edge.valid() && edge.value() < edgeCount();
    }

    /// Outgoing edges of `node`. Throws std::out_of_range for an unknown node.
    [[nodiscard]] EdgeRange outEdges(NodeId node) const;

    /// Endpoints and cost of `edge`. Throw std::out_of_range for an unknown edge.
    [[nodiscard]] NodeId source(EdgeId edge) const;
    [[nodiscard]] NodeId target(EdgeId edge) const;
    [[nodiscard]] Duration cost(EdgeId edge) const;

    /// The edge from `from` to `to`, if any. Throws std::out_of_range for an
    /// unknown `from` node.
    [[nodiscard]] std::optional<EdgeId> findEdge(NodeId from, NodeId to) const;

    /// Throws std::out_of_range for an unknown node.
    [[nodiscard]] const NodeAttributes& attributes(NodeId node) const;

    /// True if every node has a position (vacuously true for an empty graph).
    [[nodiscard]] bool hasAllPositions() const noexcept { return all_positions_; }

    /// The smallest edge cost, or zero for a graph without edges.
    [[nodiscard]] Duration minEdgeCost() const noexcept { return min_cost_; }

private:
    friend class GraphBuilder;

    void requireNode(NodeId node) const;
    void requireEdge(EdgeId edge) const;

    std::uint64_t id_;
    std::vector<NodeAttributes> attributes_;
    std::vector<EdgeId::rep_type> offsets_;  // nodeCount() + 1 entries
    std::vector<NodeId::rep_type> sources_;
    std::vector<NodeId::rep_type> targets_;
    std::vector<Duration::rep> costs_;
    Duration min_cost_ = Duration::zero();
    bool all_positions_ = true;
};

/// Collects nodes and edges and validates them into a Graph.
///
/// Nothing is validated until build(), which reports the first problem found
/// (in insertion order) so errors are deterministic and point at the input.
class GraphBuilder {
public:
    GraphBuilder() = default;

    /// Adds a node and returns its id (ids are assigned 0, 1, 2, ...).
    NodeId addNode(NodeAttributes attributes = {});

    /// Adds a directed edge. Validated by build().
    void addEdge(NodeId from, NodeId to, Duration cost);

    /// Adds the two directed edges from -> to and to -> from with equal cost.
    void addBidirectionalEdge(NodeId a, NodeId b, Duration cost);

    [[nodiscard]] std::size_t nodeCount() const noexcept { return attributes_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edges_.size(); }

    /// Validates the input and produces the graph. The builder is consumed.
    [[nodiscard]] Result<Graph, GraphError> build() &&;

private:
    struct PendingEdge {
        NodeId from;
        NodeId to;
        Duration cost;
    };

    std::vector<NodeAttributes> attributes_;
    std::vector<PendingEdge> edges_;
};

}  // namespace mapc
