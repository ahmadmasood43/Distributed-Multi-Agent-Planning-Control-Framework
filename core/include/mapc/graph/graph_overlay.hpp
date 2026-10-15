#pragma once

#include "mapc/common/result.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/graph.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace mapc {

enum class OverlayError : std::uint8_t {
    UnknownEdge,   ///< the edge id does not belong to the overlay's graph
    CostBelowBase  ///< overlays may only raise costs (see GraphOverlay)
};

[[nodiscard]] std::string_view toString(OverlayError error) noexcept;

/// Dynamic changes to a Graph: blocked edges and raised traversal costs.
///
/// The overlay is sized for one specific graph (identified by Graph::id()) but
/// does not reference it, so it can be copied into world snapshots cheaply and
/// the graph can be moved freely.
///
/// Costs can only be *raised* above the graph's base cost. Every heuristic in
/// this project is admissible for the base costs, so it stays admissible (and
/// consistent) under any overlay; lowering a cost would silently break that,
/// which is why it is rejected. To make an edge cheaper, rebuild the graph.
///
/// Not thread-safe for concurrent modification; readers on other threads must
/// work on their own copy (or an immutable snapshot).
class GraphOverlay {
public:
    explicit GraphOverlay(const Graph& graph);

    [[nodiscard]] std::uint64_t graphId() const noexcept { return graph_id_; }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return costs_.size(); }

    /// Incremented by every call that changes the overlay's state.
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }

    Result<void, OverlayError> block(EdgeId edge);
    Result<void, OverlayError> unblock(EdgeId edge);

    /// Sets the effective cost of `edge`. Must be >= the graph's base cost;
    /// setting it equal to the base cost removes the override.
    Result<void, OverlayError> setCost(EdgeId edge, Duration cost);
    Result<void, OverlayError> resetCost(EdgeId edge);

    /// Removes every block and cost override.
    void clear();

    /// Throw std::out_of_range for an unknown edge.
    [[nodiscard]] bool isBlocked(EdgeId edge) const;
    /// The effective cost (base cost unless overridden), ignoring blocking.
    [[nodiscard]] Duration cost(EdgeId edge) const;

    [[nodiscard]] std::size_t blockedCount() const noexcept { return blocked_count_; }

private:
    [[nodiscard]] bool known(EdgeId edge) const noexcept {
        return edge.valid() && edge.value() < costs_.size();
    }

    std::uint64_t graph_id_;
    std::vector<Duration::rep> base_costs_;
    std::vector<Duration::rep> costs_;
    std::vector<bool> blocked_;
    std::size_t blocked_count_ = 0;
    std::uint64_t version_ = 0;
};

/// Read-only view of a graph with an optional overlay: what planners search.
///
/// Holds pointers only; the graph and overlay must outlive the view.
class GraphView {
public:
    explicit GraphView(const Graph& graph) noexcept : graph_(&graph) {}

    /// Throws std::invalid_argument if the overlay was made for another graph.
    GraphView(const Graph& graph, const GraphOverlay& overlay);

    [[nodiscard]] const Graph& graph() const noexcept { return *graph_; }
    [[nodiscard]] const GraphOverlay* overlay() const noexcept { return overlay_; }

    /// False if the edge is blocked by the overlay.
    [[nodiscard]] bool traversable(EdgeId edge) const {
        return overlay_ == nullptr || !overlay_->isBlocked(edge);
    }

    /// Effective traversal cost (overlay override, else the base cost).
    [[nodiscard]] Duration cost(EdgeId edge) const {
        return overlay_ == nullptr ? graph_->cost(edge) : overlay_->cost(edge);
    }

private:
    const Graph* graph_;
    const GraphOverlay* overlay_ = nullptr;
};

}  // namespace mapc
