#include "mapc/io/graph_yaml.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mapc::io {

struct NamedGraphAccess {
    static std::unordered_map<std::string, NodeId>& index(NamedGraph& graph) {
        return graph.index_;
    }
};

std::optional<NodeId> NamedGraph::find(std::string_view name) const {
    const auto it = index_.find(std::string(name));
    if (it == index_.end()) {
        return std::nullopt;
    }
    return it->second;
}

const std::string& NamedGraph::nameOf(NodeId node) const {
    if (!node.valid() || node.value() >= names.size()) {
        throw std::out_of_range("NamedGraph: unknown " + toString(node));
    }
    return names[node.value()];
}

namespace {

/// Error at the (1-based) line of a YAML node.
LoadError at(const YAML::Node& node, std::string message) {
    const YAML::Mark mark = node.Mark();
    const std::size_t line = mark.line >= 0 ? static_cast<std::size_t>(mark.line) + 1 : 0;
    return LoadError{line, std::move(message)};
}

/// Fails with an error naming the first key of `map` not in `allowed`.
std::optional<LoadError> checkKeys(const YAML::Node& map, const std::set<std::string>& allowed,
                                   std::string_view what) {
    for (const auto& entry : map) {
        const auto key = entry.first.as<std::string>();
        if (allowed.count(key) == 0) {
            return at(entry.first, "unknown key '" + key + "' in " + std::string(what));
        }
    }
    return std::nullopt;
}

std::optional<LoadError> readScalarString(const YAML::Node& parent, const char* key,
                                          std::string_view what, std::string& out) {
    const YAML::Node value = parent[key];
    if (!value) {
        return at(parent, std::string(what) + " is missing '" + key + "'");
    }
    if (!value.IsScalar() || value.Scalar().empty()) {
        return at(value, "'" + std::string(key) + "' must be a non-empty scalar");
    }
    out = value.Scalar();
    return std::nullopt;
}

std::optional<LoadError> readInteger(const YAML::Node& value, const char* key, std::int64_t min,
                                     std::int64_t max, std::int64_t& out) {
    if (!value.IsScalar()) {
        return at(value, "'" + std::string(key) + "' must be an integer");
    }
    try {
        out = value.as<std::int64_t>();
    } catch (const YAML::Exception&) {
        return at(value,
                  "'" + std::string(key) + "' must be an integer (got '" + value.Scalar() + "')");
    }
    if (out < min || out > max) {
        return at(value, "'" + std::string(key) + "' must be between " + std::to_string(min) +
                             " and " + std::to_string(max));
    }
    return std::nullopt;
}

Result<NamedGraph, LoadError> parseDocument(const YAML::Node& root) {
    if (!root.IsMap()) {
        return err(at(root, "the document must be a mapping with 'nodes' and 'edges'"));
    }
    if (auto error = checkKeys(root, {"nodes", "edges"}, "the document")) {
        return err(std::move(*error));
    }
    const YAML::Node nodes = root["nodes"];
    if (!nodes || !nodes.IsSequence()) {
        return err(at(root, "'nodes' must be a sequence"));
    }
    const YAML::Node edges = root["edges"];
    if (edges && !edges.IsSequence() && !edges.IsNull()) {
        return err(at(edges, "'edges' must be a sequence"));
    }

    NamedGraph result;
    auto& index = NamedGraphAccess::index(result);
    GraphBuilder builder;
    std::vector<std::size_t> node_lines;
    std::vector<std::size_t> edge_lines;

    for (const YAML::Node& node : nodes) {
        if (!node.IsMap()) {
            return err(at(node, "each node must be a mapping"));
        }
        if (auto error = checkKeys(node, {"id", "position", "capacity"}, "a node")) {
            return err(std::move(*error));
        }
        std::string id;
        if (auto error = readScalarString(node, "id", "a node", id)) {
            return err(std::move(*error));
        }
        if (index.count(id) != 0) {
            return err(at(node["id"], "duplicate node id '" + id + "'"));
        }

        NodeAttributes attributes;
        if (const YAML::Node position = node["position"]) {
            if (!position.IsSequence() || position.size() != 2) {
                return err(at(position, "'position' must be a sequence [x, y]"));
            }
            try {
                attributes.position = Point2{position[0].as<double>(), position[1].as<double>()};
            } catch (const YAML::Exception&) {
                return err(at(position, "'position' coordinates must be numbers"));
            }
        }
        if (const YAML::Node capacity = node["capacity"]) {
            std::int64_t value = 0;
            if (auto error = readInteger(capacity, "capacity", 1,
                                         std::numeric_limits<std::uint32_t>::max(), value)) {
                return err(std::move(*error));
            }
            attributes.capacity = static_cast<std::uint32_t>(value);
        }

        const NodeId added = builder.addNode(attributes);
        index.emplace(id, added);
        result.names.push_back(std::move(id));
        node_lines.push_back(at(node, "").line);
    }

    if (edges && edges.IsSequence()) {
        for (const YAML::Node& edge : edges) {
            if (!edge.IsMap()) {
                return err(at(edge, "each edge must be a mapping"));
            }
            if (auto error = checkKeys(edge, {"from", "to", "cost", "bidirectional"}, "an edge")) {
                return err(std::move(*error));
            }
            std::string from;
            std::string to;
            if (auto error = readScalarString(edge, "from", "an edge", from)) {
                return err(std::move(*error));
            }
            if (auto error = readScalarString(edge, "to", "an edge", to)) {
                return err(std::move(*error));
            }
            const auto from_it = index.find(from);
            if (from_it == index.end()) {
                return err(at(edge["from"], "unknown node '" + from + "'"));
            }
            const auto to_it = index.find(to);
            if (to_it == index.end()) {
                return err(at(edge["to"], "unknown node '" + to + "'"));
            }
            const YAML::Node cost_node = edge["cost"];
            if (!cost_node) {
                return err(at(edge, "an edge is missing 'cost'"));
            }
            std::int64_t cost = 0;
            if (auto error = readInteger(cost_node, "cost", 1,
                                         std::numeric_limits<std::int64_t>::max(), cost)) {
                return err(std::move(*error));
            }
            bool bidirectional = false;
            if (const YAML::Node flag = edge["bidirectional"]) {
                try {
                    bidirectional = flag.as<bool>();
                } catch (const YAML::Exception&) {
                    return err(at(flag, "'bidirectional' must be true or false"));
                }
            }
            const std::size_t line = at(edge, "").line;
            builder.addEdge(from_it->second, to_it->second, Duration{cost});
            edge_lines.push_back(line);
            if (bidirectional) {
                builder.addEdge(to_it->second, from_it->second, Duration{cost});
                edge_lines.push_back(line);
            }
        }
    }

    auto graph = std::move(builder).build();
    if (!graph) {
        const GraphError& error = graph.error();
        std::size_t line = 0;
        if (error.edge_index && *error.edge_index < edge_lines.size()) {
            line = edge_lines[*error.edge_index];
        } else if (error.node_index && *error.node_index < node_lines.size()) {
            line = node_lines[*error.node_index];
        }
        return err(LoadError{line, std::string(toString(error.code)) + ": " + error.message});
    }
    result.graph = std::move(graph).value();
    return result;
}

}  // namespace

Result<NamedGraph, LoadError> parseGraphYaml(std::string_view text) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string(text));
    } catch (const YAML::ParserException& e) {
        const std::size_t line = e.mark.line >= 0 ? static_cast<std::size_t>(e.mark.line) + 1 : 0;
        return err(LoadError{line, "invalid YAML: " + e.msg});
    }
    try {
        return parseDocument(root);
    } catch (const YAML::Exception& e) {
        // Structural surprises not covered by the checks above.
        const std::size_t line = e.mark.line >= 0 ? static_cast<std::size_t>(e.mark.line) + 1 : 0;
        return err(LoadError{line, "invalid graph: " + e.msg});
    }
}

Result<NamedGraph, LoadError> loadGraphYaml(const std::filesystem::path& path) {
    const std::ifstream in(path, std::ios::binary);
    if (!in) {
        return err(LoadError{0, "cannot open '" + path.string() + "'"});
    }
    std::ostringstream content;
    content << in.rdbuf();
    auto graph = parseGraphYaml(content.str());
    if (!graph) {
        LoadError error = std::move(graph).error();
        error.message = path.filename().string() + ": " + error.message;
        return err(std::move(error));
    }
    return graph;
}

}  // namespace mapc::io
