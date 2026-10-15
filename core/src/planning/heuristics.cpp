#include "mapc/planning/heuristics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mapc {

namespace {

// Relative safety margin applied to the Euclidean scale, so that rounding in
// hypot() and the multiplication can never push an estimate above a true
// path cost.
constexpr double kScaleMargin = 1e-9;

}  // namespace

// --- GridHeuristic -----------------------------------------------------------

GridHeuristic::GridHeuristic(const GridMap& map)
    : graph_id_(map.graph().id()),
      cardinal_(map.options().cardinal_cost.count()),
      diagonal_(std::min(map.options().diagonal_cost.count(),
                         detail::saturatingMul(2, map.options().cardinal_cost.count()))),
      eight_(map.options().connectivity == GridConnectivity::Eight) {
    cells_.reserve(map.graph().nodeCount());
    for (std::size_t i = 0; i < map.graph().nodeCount(); ++i) {
        cells_.push_back(map.cellOf(NodeId{static_cast<NodeId::rep_type>(i)}));
    }
}

bool GridHeuristic::compatibleWith(const Graph& graph) const noexcept {
    return graph.id() == graph_id_;
}

Duration GridHeuristic::estimate(NodeId node, NodeId goal) const {
    if (!node.valid() || !goal.valid() || node.value() >= cells_.size() ||
        goal.value() >= cells_.size()) {
        throw std::out_of_range("GridHeuristic: unknown node");
    }
    const GridCell a = cells_[node.value()];
    const GridCell b = cells_[goal.value()];
    const auto dx = static_cast<Duration::rep>(a.x > b.x ? a.x - b.x : b.x - a.x);
    const auto dy = static_cast<Duration::rep>(a.y > b.y ? a.y - b.y : b.y - a.y);
    if (!eight_) {
        return Duration{detail::saturatingMul(cardinal_, dx + dy)};
    }
    if (diagonal_ < cardinal_) {
        // Diagonals are the cheapest moves: zig-zagging diagonally can beat a
        // straight line, so the octile formula would overestimate. Every move
        // shrinks the Chebyshev distance by at most one, so this is a
        // consistent lower bound.
        return Duration{detail::saturatingMul(diagonal_, std::max(dx, dy))};
    }
    const Duration::rep diagonal_steps = std::min(dx, dy);
    const Duration::rep straight_steps = std::max(dx, dy) - diagonal_steps;
    return Duration{detail::saturatingAdd(detail::saturatingMul(diagonal_, diagonal_steps),
                                          detail::saturatingMul(cardinal_, straight_steps))};
}

// --- EuclideanHeuristic ------------------------------------------------------

Result<EuclideanHeuristic, std::string> EuclideanHeuristic::create(const Graph& graph) {
    std::vector<Point2> positions;
    positions.reserve(graph.nodeCount());
    for (std::size_t i = 0; i < graph.nodeCount(); ++i) {
        const auto& position = graph.attributes(NodeId{static_cast<NodeId::rep_type>(i)}).position;
        if (!position) {
            return err(std::string("every node needs a position for the Euclidean heuristic"));
        }
        positions.push_back(*position);
    }

    // The scale is the cheapest cost per unit of straight-line distance over
    // all edges. Edges between coincident nodes do not constrain it.
    double scale = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < graph.edgeCount(); ++i) {
        const EdgeId edge{static_cast<EdgeId::rep_type>(i)};
        const double length =
            distance(positions[graph.source(edge).value()], positions[graph.target(edge).value()]);
        if (length > 0.0) {
            scale = std::min(scale, static_cast<double>(graph.cost(edge).count()) / length);
        }
    }
    if (!std::isfinite(scale)) {
        scale = 0.0;  // no edge with positive length: no useful estimate
    }
    scale *= 1.0 - kScaleMargin;
    return EuclideanHeuristic(graph.id(), std::move(positions), scale);
}

bool EuclideanHeuristic::compatibleWith(const Graph& graph) const noexcept {
    return graph.id() == graph_id_;
}

Duration EuclideanHeuristic::estimate(NodeId node, NodeId goal) const {
    if (!node.valid() || !goal.valid() || node.value() >= positions_.size() ||
        goal.value() >= positions_.size()) {
        throw std::out_of_range("EuclideanHeuristic: unknown node");
    }
    const double value =
        std::floor(distance(positions_[node.value()], positions_[goal.value()]) * scale_);
    if (!(value < static_cast<double>(detail::kTickMax))) {
        return Duration::max();
    }
    return Duration{static_cast<Duration::rep>(value)};
}

}  // namespace mapc
