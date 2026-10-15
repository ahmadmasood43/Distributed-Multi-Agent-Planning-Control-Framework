#pragma once

#include "mapc/common/result.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/graph.hpp"
#include "mapc/graph/load_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mapc {

enum class GridConnectivity : std::uint8_t {
    Four,  ///< moves to the 4 orthogonal neighbours
    Eight  ///< also diagonal moves, without cutting blocked corners
};

struct GridOptions {
    GridConnectivity connectivity = GridConnectivity::Four;
    /// Cost of an orthogonal move. Must be >= 1 tick.
    Duration cardinal_cost{1};
    /// Cost of a diagonal move (Eight only). Must be >= 1 tick. Integer costs
    /// cannot represent sqrt(2) exactly; a ratio such as 10/14 or 5/7 is a
    /// common approximation.
    Duration diagonal_cost{1};
};

/// A cell of a grid map; x grows to the right, y grows downwards (row index).
struct GridCell {
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    friend bool operator==(GridCell a, GridCell b) noexcept { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(GridCell a, GridCell b) noexcept { return !(a == b); }
};

/// An occupancy grid and the Graph of its passable cells.
///
/// Every passable cell is a node, numbered row by row (y, then x), with its
/// cell coordinates as the node position. Edges connect passable neighbours
/// in both directions. With Eight connectivity, a diagonal move is only added
/// when both orthogonal cells it passes are passable (no corner cutting), as
/// in the MovingAI benchmarks.
class GridMap {
public:
    /// Builds a map from rows of '.' (passable) and any other character
    /// (blocked). Useful for tests and small examples. All rows must have the
    /// same length.
    [[nodiscard]] static Result<GridMap, LoadError> fromRows(const std::vector<std::string>& rows,
                                                             GridOptions options = {});

    /// Parses the MovingAI .map format:
    ///
    ///     type octile
    ///     height <H>
    ///     width <W>
    ///     map
    ///     <H rows of W characters>
    ///
    /// '.', 'G' and 'S' are passable; '@', 'O', 'T' and 'W' are blocked
    /// (water is treated as blocked rather than as a separate terrain class).
    /// Windows line endings and trailing blank lines are accepted.
    [[nodiscard]] static Result<GridMap, LoadError> parseMovingAi(std::string_view text,
                                                                  GridOptions options = {});

    /// Reads and parses a MovingAI .map file.
    [[nodiscard]] static Result<GridMap, LoadError> loadMovingAi(const std::filesystem::path& path,
                                                                 GridOptions options = {});

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const GridOptions& options() const noexcept { return options_; }
    [[nodiscard]] const Graph& graph() const noexcept { return graph_; }

    /// False for blocked cells and for coordinates outside the map.
    [[nodiscard]] bool passable(std::uint32_t x, std::uint32_t y) const noexcept;

    /// The node of a passable cell; nullopt for blocked or outside cells.
    [[nodiscard]] std::optional<NodeId> nodeAt(std::uint32_t x, std::uint32_t y) const noexcept;

    /// The cell of a node. Throws std::out_of_range for an unknown node.
    [[nodiscard]] GridCell cellOf(NodeId node) const;

private:
    GridMap() = default;

    [[nodiscard]] static Result<GridMap, LoadError> build(std::uint32_t width, std::uint32_t height,
                                                          std::vector<bool> passable,
                                                          GridOptions options);

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    GridOptions options_;
    std::vector<NodeId::rep_type> node_of_cell_;  // width * height, invalid if blocked
    std::vector<GridCell> cell_of_node_;
    Graph graph_;
};

}  // namespace mapc
