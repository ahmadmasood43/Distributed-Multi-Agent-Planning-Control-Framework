#include "mapc/graph/grid_map.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mapc {
namespace {

GridMap mapOrDie(Result<GridMap, LoadError> result) {
    if (!result) {
        throw std::runtime_error(result.error().describe());
    }
    return std::move(result).value();
}

std::set<std::pair<std::uint32_t, std::uint32_t>> neighbours(const GridMap& map, std::uint32_t x,
                                                             std::uint32_t y) {
    std::set<std::pair<std::uint32_t, std::uint32_t>> out;
    for (const EdgeId edge : map.graph().outEdges(*map.nodeAt(x, y))) {
        const GridCell cell = map.cellOf(map.graph().target(edge));
        out.emplace(cell.x, cell.y);
    }
    return out;
}

// --- fromRows ----------------------------------------------------------------------

TEST(GridMapTest, NodesArePassableCellsInRowMajorOrder) {
    const GridMap map = mapOrDie(GridMap::fromRows({".#.", "..#"}));
    EXPECT_EQ(map.width(), 3U);
    EXPECT_EQ(map.height(), 2U);
    EXPECT_EQ(map.graph().nodeCount(), 4U);
    EXPECT_EQ(map.nodeAt(0, 0), NodeId{0});
    EXPECT_EQ(map.nodeAt(2, 0), NodeId{1});
    EXPECT_EQ(map.nodeAt(0, 1), NodeId{2});
    EXPECT_EQ(map.nodeAt(1, 1), NodeId{3});
    EXPECT_FALSE(map.nodeAt(1, 0).has_value());
    EXPECT_FALSE(map.passable(2, 1));
    EXPECT_TRUE(map.passable(1, 1));
    EXPECT_EQ(map.cellOf(NodeId{3}), (GridCell{1, 1}));
    EXPECT_EQ(map.graph().attributes(NodeId{1}).position, (Point2{2.0, 0.0}));
    EXPECT_TRUE(map.graph().hasAllPositions());
}

TEST(GridMapTest, OutsideCoordinatesAreNotPassable) {
    const GridMap map = mapOrDie(GridMap::fromRows({".."}));
    EXPECT_FALSE(map.passable(2, 0));
    EXPECT_FALSE(map.nodeAt(0, 1).has_value());
    EXPECT_FALSE(map.nodeAt(UINT32_MAX, UINT32_MAX).has_value());
    EXPECT_THROW((void)map.cellOf(NodeId{2}), std::out_of_range);
    EXPECT_THROW((void)map.cellOf(NodeId::invalid()), std::out_of_range);
}

TEST(GridMapTest, FourConnectivityLinksOrthogonalNeighboursBothWays) {
    const GridMap map = mapOrDie(GridMap::fromRows({"...", "...", "..."}));
    using Cells = std::set<std::pair<std::uint32_t, std::uint32_t>>;
    EXPECT_EQ(neighbours(map, 1, 1), (Cells{{1, 0}, {2, 1}, {1, 2}, {0, 1}}));
    EXPECT_EQ(neighbours(map, 0, 0), (Cells{{1, 0}, {0, 1}}));
    EXPECT_EQ(map.graph().edgeCount(), 24U);  // 12 adjacent pairs, both directions
    for (std::size_t e = 0; e < map.graph().edgeCount(); ++e) {
        EXPECT_EQ(map.graph().cost(EdgeId{static_cast<EdgeId::rep_type>(e)}), Duration{1});
    }
}

TEST(GridMapTest, EightConnectivityAddsDiagonalsWithoutCuttingCorners) {
    GridOptions options;
    options.connectivity = GridConnectivity::Eight;
    options.cardinal_cost = Duration{10};
    options.diagonal_cost = Duration{14};
    const GridMap map = mapOrDie(GridMap::fromRows({"...", ".#.", "..."}, options));
    using Cells = std::set<std::pair<std::uint32_t, std::uint32_t>>;
    // From the corner (0,0), the diagonal to (1,1) is blocked by the obstacle
    // itself, so only the two orthogonal moves remain.
    EXPECT_EQ(neighbours(map, 0, 0), (Cells{{1, 0}, {0, 1}}));
    // (1,0) cannot cut the obstacle's corner to reach (0,1) or (2,1).
    EXPECT_EQ(neighbours(map, 1, 0), (Cells{{0, 0}, {2, 0}}));

    const GridMap open = mapOrDie(GridMap::fromRows({"..", ".."}, options));
    const NodeId a = *open.nodeAt(0, 0);
    const NodeId d = *open.nodeAt(1, 1);
    ASSERT_TRUE(open.graph().findEdge(a, d).has_value());
    EXPECT_EQ(open.graph().cost(*open.graph().findEdge(a, d)), Duration{14});
    EXPECT_EQ(open.graph().cost(*open.graph().findEdge(a, *open.nodeAt(1, 0))), Duration{10});
}

TEST(GridMapTest, DiagonalsNeedBothOrthogonalCells) {
    GridOptions options;
    options.connectivity = GridConnectivity::Eight;
    const GridMap map = mapOrDie(GridMap::fromRows({".#", ".."}, options));
    // (0,0) -> (1,1) would pass the blocked (1,0): not allowed.
    EXPECT_FALSE(map.graph().findEdge(*map.nodeAt(0, 0), *map.nodeAt(1, 1)).has_value());
}

TEST(GridMapTest, EdgeOrderIsDeterministic) {
    const GridMap a = mapOrDie(GridMap::fromRows({"...", "..."}));
    const GridMap b = mapOrDie(GridMap::fromRows({"...", "..."}));
    ASSERT_EQ(a.graph().edgeCount(), b.graph().edgeCount());
    for (std::size_t e = 0; e < a.graph().edgeCount(); ++e) {
        const EdgeId edge{static_cast<EdgeId::rep_type>(e)};
        EXPECT_EQ(a.graph().source(edge), b.graph().source(edge));
        EXPECT_EQ(a.graph().target(edge), b.graph().target(edge));
    }
}

TEST(GridMapTest, EmptyAndFullyBlockedMaps) {
    const GridMap empty = mapOrDie(GridMap::fromRows({}));
    EXPECT_EQ(empty.width(), 0U);
    EXPECT_EQ(empty.graph().nodeCount(), 0U);
    const GridMap blocked = mapOrDie(GridMap::fromRows({"##", "##"}));
    EXPECT_EQ(blocked.graph().nodeCount(), 0U);
    EXPECT_EQ(blocked.graph().edgeCount(), 0U);
}

TEST(GridMapTest, RaggedRowsAreRejected) {
    const auto result = GridMap::fromRows({"...", ".."});
    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().line, 2U);
}

TEST(GridMapTest, InvalidOptionsAreRejected) {
    GridOptions zero_cardinal;
    zero_cardinal.cardinal_cost = Duration{0};
    EXPECT_TRUE(GridMap::fromRows({".."}, zero_cardinal).hasError());

    GridOptions zero_diagonal;
    zero_diagonal.connectivity = GridConnectivity::Eight;
    zero_diagonal.diagonal_cost = Duration{0};
    EXPECT_TRUE(GridMap::fromRows({".."}, zero_diagonal).hasError());

    // The diagonal cost is irrelevant (and unchecked) for 4-connected maps.
    GridOptions four;
    four.diagonal_cost = Duration{0};
    EXPECT_TRUE(GridMap::fromRows({".."}, four).hasValue());
}

// --- MovingAI format ---------------------------------------------------------------

constexpr const char* kSmallMap =
    "type octile\n"
    "height 3\n"
    "width 4\n"
    "map\n"
    ".G@T\n"
    "S.O.\n"
    "..W.\n";

TEST(MovingAiTest, ParsesTerrainClasses) {
    const GridMap map = mapOrDie(GridMap::parseMovingAi(kSmallMap));
    EXPECT_EQ(map.width(), 4U);
    EXPECT_EQ(map.height(), 3U);
    EXPECT_TRUE(map.passable(0, 0));   // .
    EXPECT_TRUE(map.passable(1, 0));   // G
    EXPECT_FALSE(map.passable(2, 0));  // @
    EXPECT_FALSE(map.passable(3, 0));  // T
    EXPECT_TRUE(map.passable(0, 1));   // S
    EXPECT_FALSE(map.passable(2, 1));  // O
    EXPECT_FALSE(map.passable(2, 2));  // W (treated as blocked)
    EXPECT_EQ(map.graph().nodeCount(), 8U);
}

TEST(MovingAiTest, AcceptsCrlfAndTrailingBlankLines) {
    const std::string crlf = "type octile\r\nheight 1\r\nwidth 2\r\nmap\r\n..\r\n\r\n\r\n";
    const GridMap map = mapOrDie(GridMap::parseMovingAi(crlf));
    EXPECT_EQ(map.graph().nodeCount(), 2U);
}

TEST(MovingAiTest, AcceptsWidthBeforeHeightAndNoFinalNewline) {
    const GridMap map = mapOrDie(GridMap::parseMovingAi("type octile\nwidth 2\nheight 1\nmap\n.."));
    EXPECT_EQ(map.width(), 2U);
    EXPECT_EQ(map.height(), 1U);
}

struct BadMap {
    const char* name;
    const char* text;
    std::size_t line;
    const char* message_part;
};

class MovingAiErrorTest : public ::testing::TestWithParam<BadMap> {};

TEST_P(MovingAiErrorTest, ReportsLineAndReason) {
    const BadMap& bad = GetParam();
    const auto result = GridMap::parseMovingAi(bad.text);
    ASSERT_TRUE(result.hasError()) << bad.name;
    EXPECT_EQ(result.error().line, bad.line) << bad.name << ": " << result.error().describe();
    EXPECT_NE(result.error().message.find(bad.message_part), std::string::npos)
        << bad.name << ": " << result.error().message;
}

INSTANTIATE_TEST_SUITE_P(
    BadInputs, MovingAiErrorTest,
    ::testing::Values(
        BadMap{"empty", "", 1, "type"}, BadMap{"no_type", "height 1\nwidth 1\nmap\n.\n", 1, "type"},
        BadMap{"no_map_line", "type octile\nheight 1\nwidth 1\n", 4, "missing 'map'"},
        BadMap{"missing_width", "type octile\nheight 1\nmap\n.\n", 3, "missing 'width'"},
        BadMap{"missing_height", "type octile\nwidth 1\nmap\n.\n", 3, "missing 'height'"},
        BadMap{"duplicate_height", "type octile\nheight 1\nheight 1\nwidth 1\nmap\n.\n", 3,
               "duplicate"},
        BadMap{"zero_width", "type octile\nheight 1\nwidth 0\nmap\n", 3, "invalid width"},
        BadMap{"negative", "type octile\nheight -1\nwidth 1\nmap\n.\n", 2, "invalid height"},
        BadMap{"not_a_number", "type octile\nheight x\nwidth 1\nmap\n.\n", 2, "invalid height"},
        BadMap{"huge", "type octile\nheight 99999999999\nwidth 1\nmap\n.\n", 2, "invalid height"},
        BadMap{"no_value", "type octile\nheight\nwidth 1\nmap\n.\n", 2, "invalid height"},
        BadMap{"unknown_header", "type octile\ndepth 1\nmap\n", 2, "unexpected header"},
        BadMap{"short_row", "type octile\nheight 1\nwidth 3\nmap\n..\n", 5, "expected 3"},
        BadMap{"long_row", "type octile\nheight 1\nwidth 1\nmap\n..\n", 5, "expected 1"},
        BadMap{"missing_rows", "type octile\nheight 3\nwidth 1\nmap\n.\n", 6, "found 1"},
        BadMap{"bad_terrain", "type octile\nheight 1\nwidth 3\nmap\n.x.\n", 5, "column 2"},
        BadMap{"extra_rows", "type octile\nheight 1\nwidth 1\nmap\n.\n.\n", 6, "after the map"}),
    [](const ::testing::TestParamInfo<BadMap>& param_info) {
        return std::string(param_info.param.name);
    });

TEST(MovingAiTest, LoadsFromFileAndReportsMissingFiles) {
    const auto dir = std::filesystem::temp_directory_path() / "mapc_grid_map_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "small.map";
    {
        std::ofstream out(path, std::ios::binary);
        out << kSmallMap;
    }
    const GridMap map = mapOrDie(GridMap::loadMovingAi(path));
    EXPECT_EQ(map.graph().nodeCount(), 8U);

    const auto bad = dir / "bad.map";
    {
        std::ofstream out(bad, std::ios::binary);
        out << "type octile\nheight 1\nwidth 1\nmap\nx\n";
    }
    const auto bad_result = GridMap::loadMovingAi(bad);
    ASSERT_TRUE(bad_result.hasError());
    EXPECT_EQ(bad_result.error().line, 5U);
    EXPECT_NE(bad_result.error().message.find("bad.map"), std::string::npos);

    const auto missing = GridMap::loadMovingAi(dir / "does_not_exist.map");
    ASSERT_TRUE(missing.hasError());
    EXPECT_EQ(missing.error().line, 0U);
    EXPECT_NE(missing.error().describe().find("cannot open"), std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST(LoadErrorTest, DescribeIncludesLineWhenKnown) {
    EXPECT_EQ((LoadError{3, "bad"}).describe(), "line 3: bad");
    EXPECT_EQ((LoadError{0, "bad"}).describe(), "bad");
}

}  // namespace
}  // namespace mapc
