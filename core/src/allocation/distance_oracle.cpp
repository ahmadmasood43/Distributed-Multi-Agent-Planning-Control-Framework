#include "mapc/allocation/distance_oracle.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace mapc {

DistanceOracle::DistanceOracle(GraphView view)
    : view_(view),
      overlay_version_(view.overlay() != nullptr ? view.overlay()->version() : 0),
      row_of_node_(view.graph().nodeCount(), kNoRow) {}

std::optional<Duration> DistanceOracle::distance(NodeId from, NodeId to) {
    const Graph& graph = view_.graph();
    if (!graph.contains(from) || !graph.contains(to)) {
        throw std::out_of_range("DistanceOracle: " + toString(graph.contains(from) ? to : from) +
                                " is not a node of the graph");
    }
    const Duration::rep d = row(from)[to.value()];
    if (d == kUnreachable) {
        return std::nullopt;
    }
    return Duration{d};
}

void DistanceOracle::clear() noexcept {
    rows_.clear();
    std::fill(row_of_node_.begin(), row_of_node_.end(), kNoRow);
}

const std::vector<Duration::rep>& DistanceOracle::row(NodeId source) {
    if (const GraphOverlay* overlay = view_.overlay();
        overlay != nullptr && overlay->version() != overlay_version_) {
        clear();
        overlay_version_ = overlay->version();
    }
    std::uint32_t& index = row_of_node_[source.value()];
    if (index == kNoRow) {
        std::vector<Duration::rep> dist;
        search(source, dist);
        ++searches_;
        index = static_cast<std::uint32_t>(rows_.size());
        rows_.push_back(std::move(dist));
    }
    return rows_[index];
}

void DistanceOracle::search(NodeId source, std::vector<Duration::rep>& dist) const {
    const Graph& graph = view_.graph();
    dist.assign(graph.nodeCount(), kUnreachable);
    using Entry = std::pair<Duration::rep, NodeId::rep_type>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    dist[source.value()] = 0;
    open.emplace(0, source.value());
    while (!open.empty()) {
        const auto [d, node] = open.top();
        open.pop();
        if (d != dist[node]) {
            continue;  // stale entry; the node was reached more cheaply
        }
        for (const EdgeId edge : graph.outEdges(NodeId{node})) {
            if (!view_.traversable(edge)) {
                continue;
            }
            const NodeId::rep_type next = graph.target(edge).value();
            const Duration::rep candidate = detail::saturatingAdd(d, view_.cost(edge).count());
            if (dist[next] == kUnreachable || candidate < dist[next]) {
                dist[next] = candidate;
                open.emplace(candidate, next);
            }
        }
    }
}

}  // namespace mapc
