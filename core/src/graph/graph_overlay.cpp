#include "mapc/graph/graph_overlay.hpp"

#include <stdexcept>

namespace mapc {

std::string_view toString(OverlayError error) noexcept {
    switch (error) {
        case OverlayError::UnknownEdge:
            return "unknown edge";
        case OverlayError::CostBelowBase:
            return "cost below base cost";
    }
    return "unknown error";
}

GraphOverlay::GraphOverlay(const Graph& graph)
    : graph_id_(graph.id()),
      base_costs_(graph.edgeCount()),
      costs_(graph.edgeCount()),
      blocked_(graph.edgeCount(), false) {
    for (std::size_t i = 0; i < graph.edgeCount(); ++i) {
        const auto cost = graph.cost(EdgeId{static_cast<EdgeId::rep_type>(i)}).count();
        base_costs_[i] = cost;
        costs_[i] = cost;
    }
}

Result<void, OverlayError> GraphOverlay::block(EdgeId edge) {
    if (!known(edge)) {
        return err(OverlayError::UnknownEdge);
    }
    if (!blocked_[edge.value()]) {
        blocked_[edge.value()] = true;
        ++blocked_count_;
        ++version_;
    }
    return {};
}

Result<void, OverlayError> GraphOverlay::unblock(EdgeId edge) {
    if (!known(edge)) {
        return err(OverlayError::UnknownEdge);
    }
    if (blocked_[edge.value()]) {
        blocked_[edge.value()] = false;
        --blocked_count_;
        ++version_;
    }
    return {};
}

Result<void, OverlayError> GraphOverlay::setCost(EdgeId edge, Duration cost) {
    if (!known(edge)) {
        return err(OverlayError::UnknownEdge);
    }
    if (cost.count() < base_costs_[edge.value()]) {
        return err(OverlayError::CostBelowBase);
    }
    if (costs_[edge.value()] != cost.count()) {
        costs_[edge.value()] = cost.count();
        ++version_;
    }
    return {};
}

Result<void, OverlayError> GraphOverlay::resetCost(EdgeId edge) {
    if (!known(edge)) {
        return err(OverlayError::UnknownEdge);
    }
    return setCost(edge, Duration{base_costs_[edge.value()]});
}

void GraphOverlay::clear() {
    bool changed = blocked_count_ != 0;
    for (std::size_t i = 0; i < costs_.size(); ++i) {
        changed = changed || costs_[i] != base_costs_[i];
        costs_[i] = base_costs_[i];
        blocked_[i] = false;
    }
    blocked_count_ = 0;
    if (changed) {
        ++version_;
    }
}

bool GraphOverlay::isBlocked(EdgeId edge) const {
    if (!known(edge)) {
        throw std::out_of_range("GraphOverlay: unknown " + toString(edge));
    }
    return blocked_[edge.value()];
}

Duration GraphOverlay::cost(EdgeId edge) const {
    if (!known(edge)) {
        throw std::out_of_range("GraphOverlay: unknown " + toString(edge));
    }
    return Duration{costs_[edge.value()]};
}

GraphView::GraphView(const Graph& graph, const GraphOverlay& overlay)
    : graph_(&graph), overlay_(&overlay) {
    if (overlay.graphId() != graph.id() || overlay.edgeCount() != graph.edgeCount()) {
        throw std::invalid_argument("GraphView: the overlay was made for a different graph");
    }
}

}  // namespace mapc
