#pragma once

#include "mapc/common/result.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/graph.hpp"
#include "mapc/graph/grid_map.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mapc {

/// A lower bound on the cost from a node to a goal.
///
/// Every implementation is admissible (never overestimates) and consistent
/// (h(u) <= cost(u, v) + h(v) for every edge) with respect to the *base*
/// costs of the graph it was made for. Because GraphOverlay can only raise
/// costs, both properties also hold under any overlay.
class Heuristic {
public:
    Heuristic() = default;
    Heuristic(const Heuristic&) = default;
    Heuristic& operator=(const Heuristic&) = default;
    Heuristic(Heuristic&&) = default;
    Heuristic& operator=(Heuristic&&) = default;
    virtual ~Heuristic() = default;

    /// Estimated cost from `node` to `goal`. Both must be nodes of a
    /// compatible graph.
    [[nodiscard]] virtual Duration estimate(NodeId node, NodeId goal) const = 0;

    /// True if this heuristic may be used with `graph`.
    [[nodiscard]] virtual bool compatibleWith(const Graph& graph) const noexcept = 0;
};

/// Always zero: turns A* into Dijkstra. Compatible with every graph.
class ZeroHeuristic final : public Heuristic {
public:
    [[nodiscard]] Duration estimate(NodeId /*node*/, NodeId /*goal*/) const override {
        return Duration::zero();
    }
    [[nodiscard]] bool compatibleWith(const Graph& /*graph*/) const noexcept override {
        return true;
    }
};

/// Obstacle-free distance on a grid, using the map's move costs: Manhattan
/// distance for 4-connected maps, octile distance for 8-connected maps.
/// Unusual cost ratios keep it a consistent lower bound: if a diagonal costs
/// more than two orthogonal moves, two orthogonal moves are counted instead;
/// if a diagonal is cheaper than an orthogonal move, the estimate is the
/// diagonal cost times the Chebyshev distance.
class GridHeuristic final : public Heuristic {
public:
    explicit GridHeuristic(const GridMap& map);

    [[nodiscard]] Duration estimate(NodeId node, NodeId goal) const override;
    [[nodiscard]] bool compatibleWith(const Graph& graph) const noexcept override;

private:
    std::uint64_t graph_id_;
    std::vector<GridCell> cells_;
    Duration::rep cardinal_;
    Duration::rep diagonal_;
    bool eight_;
};

/// Straight-line distance scaled to ticks, for graphs whose nodes have
/// positions (road networks, warehouse layouts).
///
/// The scale is the smallest cost-per-unit-length over all edges, so the
/// estimate never exceeds the cost of any path; it is then rounded down and
/// given a small safety margin against floating-point error.
class EuclideanHeuristic final : public Heuristic {
public:
    /// Fails if any node lacks a position.
    [[nodiscard]] static Result<EuclideanHeuristic, std::string> create(const Graph& graph);

    [[nodiscard]] Duration estimate(NodeId node, NodeId goal) const override;
    [[nodiscard]] bool compatibleWith(const Graph& graph) const noexcept override;

    /// Ticks per unit of distance used for the estimate.
    [[nodiscard]] double scale() const noexcept { return scale_; }

private:
    EuclideanHeuristic(std::uint64_t graph_id, std::vector<Point2> positions, double scale)
        : graph_id_(graph_id), positions_(std::move(positions)), scale_(scale) {}

    std::uint64_t graph_id_;
    std::vector<Point2> positions_;
    double scale_;
};

}  // namespace mapc
