#pragma once

// Helpers shared by the benchmarks that run on MovingAI maps.

#include "mapc/graph/graph.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace mapc::bench {

/// Nodes of the largest weakly connected region (grid edges are symmetric),
/// so any two of them are connected.
inline std::vector<NodeId> largestRegion(const Graph& graph) {
    std::vector<int> region(graph.nodeCount(), -1);
    std::vector<NodeId> best;
    int next_region = 0;
    for (std::size_t seed = 0; seed < graph.nodeCount(); ++seed) {
        if (region[seed] != -1) {
            continue;
        }
        std::vector<NodeId> members{NodeId{static_cast<NodeId::rep_type>(seed)}};
        region[seed] = next_region;
        for (std::size_t i = 0; i < members.size(); ++i) {
            for (const EdgeId edge : graph.outEdges(members[i])) {
                const NodeId next = graph.target(edge);
                if (region[next.value()] == -1) {
                    region[next.value()] = next_region;
                    members.push_back(next);
                }
            }
        }
        if (members.size() > best.size()) {
            best = std::move(members);
        }
        ++next_region;
    }
    return best;
}

}  // namespace mapc::bench
