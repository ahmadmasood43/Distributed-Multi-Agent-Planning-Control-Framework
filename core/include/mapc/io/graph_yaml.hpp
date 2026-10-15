#pragma once

#include "mapc/common/result.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/graph/graph.hpp"
#include "mapc/graph/load_error.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mapc::io {

/// A graph whose nodes carry the names they were given in the input file.
struct NamedGraph {
    Graph graph;
    std::vector<std::string> names;  // indexed by NodeId

    /// The node called `name`, if any.
    [[nodiscard]] std::optional<NodeId> find(std::string_view name) const;

    /// Throws std::out_of_range for an unknown node.
    [[nodiscard]] const std::string& nameOf(NodeId node) const;

private:
    friend struct NamedGraphAccess;
    std::unordered_map<std::string, NodeId> index_;
};

/// Parses a graph from YAML:
///
///     nodes:
///       - id: A                # required, unique
///         position: [0.0, 0.0] # optional (needed by the Euclidean heuristic)
///         capacity: 1          # optional, >= 1 (default 1)
///     edges:                   # optional
///       - from: A
///         to: B
///         cost: 5              # required integer, >= 1 tick
///         bidirectional: true  # optional (default false)
///
/// Unknown keys are errors (they usually hide a typo). Errors carry the YAML
/// line they refer to.
[[nodiscard]] Result<NamedGraph, LoadError> parseGraphYaml(std::string_view text);

/// Reads and parses a YAML graph file.
[[nodiscard]] Result<NamedGraph, LoadError> loadGraphYaml(const std::filesystem::path& path);

}  // namespace mapc::io
