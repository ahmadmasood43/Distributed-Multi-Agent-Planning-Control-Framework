#include "mapc/io/graph_yaml.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace mapc::io {
namespace {

constexpr const char* kRoadMap = R"(# A small road network.
nodes:
  - id: depot
    position: [0, 0]
    capacity: 3
  - id: cross
    position: [3.5, 4]
  - id: shop
    position: [10, 4]
edges:
  - {from: depot, to: cross, cost: 6, bidirectional: true}
  - from: cross
    to: shop
    cost: 7
)";

NamedGraph graphOrDie(Result<NamedGraph, LoadError> result) {
    if (!result) {
        throw std::runtime_error(result.error().describe());
    }
    return std::move(result).value();
}

TEST(GraphYamlTest, ParsesNodesEdgesAndAttributes) {
    const NamedGraph named = graphOrDie(parseGraphYaml(kRoadMap));
    const Graph& graph = named.graph;
    EXPECT_EQ(graph.nodeCount(), 3U);
    EXPECT_EQ(graph.edgeCount(), 3U);  // one bidirectional edge counts twice

    const NodeId depot = *named.find("depot");
    const NodeId cross = *named.find("cross");
    const NodeId shop = *named.find("shop");
    EXPECT_EQ(named.nameOf(shop), "shop");
    EXPECT_EQ(graph.attributes(depot).capacity, 3U);
    EXPECT_EQ(graph.attributes(cross).capacity, 1U);
    EXPECT_EQ(graph.attributes(cross).position, (Point2{3.5, 4.0}));
    EXPECT_TRUE(graph.hasAllPositions());

    EXPECT_EQ(graph.cost(*graph.findEdge(depot, cross)), Duration{6});
    EXPECT_EQ(graph.cost(*graph.findEdge(cross, depot)), Duration{6});
    EXPECT_EQ(graph.cost(*graph.findEdge(cross, shop)), Duration{7});
    EXPECT_FALSE(graph.findEdge(shop, cross).has_value());
}

TEST(GraphYamlTest, NodeNamesAreLookedUpExactly) {
    const NamedGraph named = graphOrDie(parseGraphYaml(kRoadMap));
    EXPECT_FALSE(named.find("Depot").has_value());
    EXPECT_FALSE(named.find("").has_value());
    EXPECT_THROW((void)named.nameOf(NodeId{3}), std::out_of_range);
    EXPECT_THROW((void)named.nameOf(NodeId::invalid()), std::out_of_range);
}

TEST(GraphYamlTest, EdgesArOptionalAndPositionsToo) {
    const NamedGraph named = graphOrDie(parseGraphYaml("nodes:\n  - id: a\n  - id: b\n"));
    EXPECT_EQ(named.graph.nodeCount(), 2U);
    EXPECT_EQ(named.graph.edgeCount(), 0U);
    EXPECT_FALSE(named.graph.hasAllPositions());

    const NamedGraph empty_edges = graphOrDie(parseGraphYaml("nodes:\n  - id: a\nedges:\n"));
    EXPECT_EQ(empty_edges.graph.edgeCount(), 0U);

    const NamedGraph numeric = graphOrDie(parseGraphYaml("nodes:\n  - id: 7\n"));
    EXPECT_TRUE(numeric.find("7").has_value());
}

struct BadYaml {
    const char* name;
    const char* text;
    std::size_t line;
    const char* message_part;
};

class GraphYamlErrorTest : public ::testing::TestWithParam<BadYaml> {};

TEST_P(GraphYamlErrorTest, ReportsLineAndReason) {
    const BadYaml& bad = GetParam();
    const auto result = parseGraphYaml(bad.text);
    ASSERT_TRUE(result.hasError()) << bad.name;
    EXPECT_EQ(result.error().line, bad.line) << bad.name << ": " << result.error().describe();
    EXPECT_NE(result.error().message.find(bad.message_part), std::string::npos)
        << bad.name << ": " << result.error().message;
}

INSTANTIATE_TEST_SUITE_P(
    BadInputs, GraphYamlErrorTest,
    ::testing::Values(
        BadYaml{"not_yaml", "nodes: [a, b\n", 2, "invalid YAML"},
        BadYaml{"not_a_map", "- a\n- b\n", 1, "must be a mapping"},
        BadYaml{"unknown_top_key", "nodes: []\nedgez: []\n", 2, "unknown key 'edgez'"},
        BadYaml{"missing_nodes", "edges: []\n", 1, "'nodes' must be a sequence"},
        BadYaml{"nodes_not_sequence", "nodes: 5\n", 1, "'nodes' must be a sequence"},
        BadYaml{"edges_not_sequence", "nodes: []\nedges: 5\n", 2, "'edges' must be a sequence"},
        BadYaml{"node_not_map", "nodes:\n  - a\n", 2, "each node must be a mapping"},
        BadYaml{"node_unknown_key", "nodes:\n  - {id: a, colour: red}\n", 2,
                "unknown key 'colour'"},
        BadYaml{"node_missing_id", "nodes:\n  - {capacity: 2}\n", 2, "missing 'id'"},
        BadYaml{"node_empty_id", "nodes:\n  - {id: ''}\n", 2, "non-empty"},
        BadYaml{"node_id_not_scalar", "nodes:\n  - {id: [1]}\n", 2, "non-empty scalar"},
        BadYaml{"duplicate_id", "nodes:\n  - id: a\n  - id: a\n", 3, "duplicate node id"},
        BadYaml{"bad_position_shape", "nodes:\n  - {id: a, position: [1]}\n", 2, "[x, y]"},
        BadYaml{"bad_position_value", "nodes:\n  - {id: a, position: [1, x]}\n", 2, "numbers"},
        BadYaml{"zero_capacity", "nodes:\n  - {id: a, capacity: 0}\n", 2, "between 1"},
        BadYaml{"text_capacity", "nodes:\n  - {id: a, capacity: lots}\n", 2, "integer"},
        BadYaml{"list_capacity", "nodes:\n  - {id: a, capacity: [1]}\n", 2, "integer"},
        BadYaml{"edge_not_map", "nodes:\n  - id: a\nedges:\n  - a\n", 4, "each edge"},
        BadYaml{"edge_unknown_key", "nodes:\n  - id: a\nedges:\n  - {form: a}\n", 4,
                "unknown key 'form'"},
        BadYaml{"edge_missing_from", "nodes:\n  - id: a\nedges:\n  - {to: a, cost: 1}\n", 4,
                "missing 'from'"},
        BadYaml{"edge_unknown_from", "nodes:\n  - id: a\nedges:\n  - {from: b, to: a, cost: 1}\n",
                4, "unknown node 'b'"},
        BadYaml{"edge_unknown_to", "nodes:\n  - id: a\nedges:\n  - {from: a, to: b, cost: 1}\n", 4,
                "unknown node 'b'"},
        BadYaml{"edge_missing_cost", "nodes:\n  - id: a\n  - id: b\nedges:\n  - {from: a, to: b}\n",
                5, "missing 'cost'"},
        BadYaml{"edge_fractional_cost",
                "nodes:\n  - id: a\n  - id: b\nedges:\n  - {from: a, to: b, cost: 1.5}\n", 5,
                "integer"},
        BadYaml{"edge_zero_cost",
                "nodes:\n  - id: a\n  - id: b\nedges:\n  - {from: a, to: b, cost: 0}\n", 5,
                "between 1"},
        BadYaml{"edge_bad_flag",
                "nodes:\n  - id: a\n  - id: b\nedges:\n  - {from: a, to: b, cost: 1, "
                "bidirectional: maybe}\n",
                5, "true or false"},
        BadYaml{"self_loop", "nodes:\n  - id: a\nedges:\n  - {from: a, to: a, cost: 1}\n", 4,
                "self loop"},
        BadYaml{"duplicate_edge",
                "nodes:\n  - id: a\n  - id: b\nedges:\n  - {from: a, to: b, cost: 1}\n"
                "  - {from: a, to: b, cost: 2}\n",
                6, "duplicate edge"},
        BadYaml{"bidirectional_duplicate",
                "nodes:\n  - id: a\n  - id: b\nedges:\n  - {from: b, to: a, cost: 1}\n"
                "  - {from: a, to: b, cost: 2, bidirectional: true}\n",
                6, "duplicate edge"},
        BadYaml{"infinite_position", "nodes:\n  - {id: a, position: [.inf, 0]}\n", 2,
                "invalid position"}),
    [](const ::testing::TestParamInfo<BadYaml>& param_info) {
        return std::string(param_info.param.name);
    });

TEST(GraphYamlTest, LoadsFromFileAndReportsMissingFiles) {
    const auto dir = std::filesystem::temp_directory_path() / "mapc_graph_yaml_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "roads.yaml";
    {
        std::ofstream out(path, std::ios::binary);
        out << kRoadMap;
    }
    EXPECT_EQ(graphOrDie(loadGraphYaml(path)).graph.nodeCount(), 3U);

    const auto bad = dir / "bad.yaml";
    {
        std::ofstream out(bad, std::ios::binary);
        out << "nodes:\n  - id: a\n  - id: a\n";
    }
    const auto bad_result = loadGraphYaml(bad);
    ASSERT_TRUE(bad_result.hasError());
    EXPECT_EQ(bad_result.error().line, 3U);
    EXPECT_NE(bad_result.error().message.find("bad.yaml"), std::string::npos);

    const auto missing = loadGraphYaml(dir / "nope.yaml");
    ASSERT_TRUE(missing.hasError());
    EXPECT_EQ(missing.error().line, 0U);
    std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace mapc::io
