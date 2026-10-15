#pragma once

#include "mapc/common/rng.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/graph.hpp"
#include "mapc/graph/graph_overlay.hpp"
#include "mapc/graph/grid_map.hpp"
#include "mapc/planning/path.hpp"
#include "mapc/planning/planner.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mapc::testing {

inline Graph buildOrThrow(GraphBuilder builder) {
    auto graph = std::move(builder).build();
    if (!graph) {
        throw std::runtime_error("test graph is invalid: " + graph.error().message);
    }
    return std::move(graph).value();
}

inline GridMap gridOrThrow(Result<GridMap, LoadError> result) {
    if (!result) {
        throw std::runtime_error("test grid is invalid: " + result.error().describe());
    }
    return std::move(result).value();
}

/// Random graph with node positions in [0, 100)^2. Each edge costs at least
/// its (rounded-up) length, so straight-line distance is informative, plus
/// some random slack. Edges are directed and may be one-way.
inline Graph randomGeometricGraph(Rng& rng, std::size_t nodes, std::size_t edges) {
    GraphBuilder builder;
    std::vector<Point2> positions;
    for (std::size_t i = 0; i < nodes; ++i) {
        positions.push_back(Point2{rng.uniformReal(0.0, 100.0), rng.uniformReal(0.0, 100.0)});
        builder.addNode(NodeAttributes{positions.back(), 1});
    }
    std::vector<std::vector<bool>> used(nodes, std::vector<bool>(nodes, false));
    for (std::size_t e = 0; e < edges && nodes > 1; ++e) {
        const auto a = static_cast<std::size_t>(rng.uniformBelow(nodes));
        const auto b = static_cast<std::size_t>(rng.uniformBelow(nodes));
        if (a == b || used[a][b]) {
            continue;
        }
        used[a][b] = true;
        const auto length =
            static_cast<Duration::rep>(std::ceil(distance(positions[a], positions[b])));
        const Duration cost{std::max<Duration::rep>(1, length) + rng.uniformInt(0, 10)};
        builder.addEdge(NodeId{static_cast<NodeId::rep_type>(a)},
                        NodeId{static_cast<NodeId::rep_type>(b)}, cost);
    }
    return buildOrThrow(std::move(builder));
}

/// Random graph without positions and with arbitrary costs in [1, 50].
inline Graph randomCostGraph(Rng& rng, std::size_t nodes, std::size_t edges) {
    GraphBuilder builder;
    for (std::size_t i = 0; i < nodes; ++i) {
        builder.addNode();
    }
    std::vector<std::vector<bool>> used(nodes, std::vector<bool>(nodes, false));
    for (std::size_t e = 0; e < edges && nodes > 1; ++e) {
        const auto a = static_cast<std::size_t>(rng.uniformBelow(nodes));
        const auto b = static_cast<std::size_t>(rng.uniformBelow(nodes));
        if (a == b || used[a][b]) {
            continue;
        }
        used[a][b] = true;
        builder.addEdge(NodeId{static_cast<NodeId::rep_type>(a)},
                        NodeId{static_cast<NodeId::rep_type>(b)}, Duration{rng.uniformInt(1, 50)});
    }
    return buildOrThrow(std::move(builder));
}

/// Random occupancy grid; `obstacle_percent` of the cells are blocked.
inline GridMap randomGrid(Rng& rng, std::uint32_t width, std::uint32_t height, int obstacle_percent,
                          GridOptions options) {
    std::vector<std::string> rows(height, std::string(width, '.'));
    for (auto& row : rows) {
        for (char& cell : row) {
            if (rng.uniformInt(0, 99) < obstacle_percent) {
                cell = '#';
            }
        }
    }
    return gridOrThrow(GridMap::fromRows(rows, options));
}

/// Shortest distances from `goal` *backwards* (cost of reaching goal from each
/// node) by Bellman-Ford: an independent reference for the planners. Blocked
/// edges and overlay costs are honoured through `view`.
inline std::vector<std::optional<Duration::rep>> distancesTo(const GraphView& view, NodeId goal) {
    const Graph& graph = view.graph();
    std::vector<std::optional<Duration::rep>> dist(graph.nodeCount());
    dist[goal.value()] = 0;
    for (std::size_t round = 0; round < graph.nodeCount(); ++round) {
        bool changed = false;
        for (std::size_t e = 0; e < graph.edgeCount(); ++e) {
            const EdgeId edge{static_cast<EdgeId::rep_type>(e)};
            if (!view.traversable(edge)) {
                continue;
            }
            const auto& to = dist[graph.target(edge).value()];
            if (!to) {
                continue;
            }
            auto& from = dist[graph.source(edge).value()];
            const Duration::rep candidate = detail::saturatingAdd(*to, view.cost(edge).count());
            if (!from || candidate < *from) {
                from = candidate;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    return dist;
}

/// Empty string if `path` is a valid timed path for `request` in `view`,
/// otherwise a description of the first problem.
inline std::string pathProblem(const GraphView& view, const PlanRequest& request,
                               const Path& path) {
    if (path.steps.empty()) {
        return "path is empty";
    }
    if (path.steps.front() != PathStep{request.start, request.start_time}) {
        return "path does not begin at the start node and start time";
    }
    if (path.steps.back().node != request.goal) {
        return "path does not end at the goal";
    }
    for (std::size_t i = 1; i < path.steps.size(); ++i) {
        const auto edge = view.graph().findEdge(path.steps[i - 1].node, path.steps[i].node);
        if (!edge) {
            return "step " + std::to_string(i) + " does not follow an edge";
        }
        if (!view.traversable(*edge)) {
            return "step " + std::to_string(i) + " uses a blocked edge";
        }
        if (path.steps[i].time != path.steps[i - 1].time + view.cost(*edge)) {
            return "step " + std::to_string(i) + " has the wrong time";
        }
    }
    if (path.cost != path.steps.back().time - path.steps.front().time) {
        return "path cost does not match its duration";
    }
    return "";
}

}  // namespace mapc::testing
