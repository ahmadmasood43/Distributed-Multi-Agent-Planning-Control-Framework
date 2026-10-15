#pragma once

#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/graph_overlay.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace mapc {

/// Exact shortest-path distances in a GraphView, cached per source node.
///
/// The first query from a node runs one full Dijkstra search from it and keeps
/// the distances to every node; later queries from the same node are O(1).
/// Allocators ask for many (agent, task) distances, and agents and task
/// sources repeat, so this is far cheaper than running a planner per pair.
/// Each cached source costs 8 bytes per graph node.
///
/// The view's overlay is honoured. If the overlay is changed through its
/// methods (its version() moves on), the cache is dropped on the next query,
/// so distances never go stale. Assigning a different overlay over the
/// referenced one is not detected; call clear() after doing that. The graph
/// and overlay must outlive the oracle.
///
/// Not thread-safe: queries fill the cache.
class DistanceOracle {
public:
    explicit DistanceOracle(GraphView view);

    /// Shortest-path cost from `from` to `to`, or nullopt if `to` cannot be
    /// reached. Throws std::out_of_range for a node that is not in the graph.
    [[nodiscard]] std::optional<Duration> distance(NodeId from, NodeId to);

    [[nodiscard]] const GraphView& view() const noexcept { return view_; }

    /// Number of source nodes whose distances are cached.
    [[nodiscard]] std::size_t cachedSources() const noexcept { return rows_.size(); }

    /// Number of single-source searches run so far (including ones whose
    /// results were later dropped).
    [[nodiscard]] std::uint64_t searches() const noexcept { return searches_; }

    /// Drops every cached distance.
    void clear() noexcept;

private:
    static constexpr Duration::rep kUnreachable = -1;
    static constexpr std::uint32_t kNoRow = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] const std::vector<Duration::rep>& row(NodeId source);
    void search(NodeId source, std::vector<Duration::rep>& dist) const;

    GraphView view_;
    std::uint64_t overlay_version_ = 0;
    std::vector<std::uint32_t> row_of_node_;
    std::vector<std::vector<Duration::rep>> rows_;
    std::uint64_t searches_ = 0;
};

}  // namespace mapc
