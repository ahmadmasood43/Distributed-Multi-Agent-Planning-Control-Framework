#include "mapc/graph/grid_map.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace mapc {

namespace {

struct Line {
    std::size_t number;  // 1-based
    std::string_view text;
};

/// Splits into lines, accepting "\n" and "\r\n" endings.
std::vector<Line> splitLines(std::string_view text) {
    std::vector<Line> lines;
    std::size_t number = 1;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(Line{number, line});
        ++number;
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return lines;
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

/// Parses a positive decimal integer no larger than `limit`.
std::optional<std::uint32_t> parseDimension(std::string_view s, std::uint32_t limit) noexcept {
    if (s.empty() || s.size() > 10) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10U + static_cast<std::uint64_t>(c - '0');
    }
    if (value == 0 || value > limit) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

bool isMovingAiPassable(char c) noexcept {
    return c == '.' || c == 'G' || c == 'S';
}
bool isMovingAiBlocked(char c) noexcept {
    return c == '@' || c == 'O' || c == 'T' || c == 'W';
}

// Largest accepted width or height. Keeps width * height comfortably inside
// the node id range.
constexpr std::uint32_t kMaxDimension = 1U << 15U;

LoadError optionsError(const GridOptions& options) {
    if (options.cardinal_cost < Duration{1}) {
        return LoadError{0, "grid cardinal cost must be at least 1 tick"};
    }
    if (options.connectivity == GridConnectivity::Eight && options.diagonal_cost < Duration{1}) {
        return LoadError{0, "grid diagonal cost must be at least 1 tick"};
    }
    return LoadError{0, ""};
}

}  // namespace

Result<GridMap, LoadError> GridMap::build(std::uint32_t width, std::uint32_t height,
                                          std::vector<bool> passable, GridOptions options) {
    if (LoadError error = optionsError(options); !error.message.empty()) {
        return err(std::move(error));
    }

    GridMap map;
    map.width_ = width;
    map.height_ = height;
    map.options_ = options;
    const std::size_t cells = static_cast<std::size_t>(width) * height;
    map.node_of_cell_.assign(cells, NodeId::kInvalidValue);

    GraphBuilder builder;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            if (passable[index]) {
                const NodeId node = builder.addNode(
                    NodeAttributes{Point2{static_cast<double>(x), static_cast<double>(y)}, 1});
                map.node_of_cell_[index] = node.value();
                map.cell_of_node_.push_back(GridCell{x, y});
            }
        }
    }

    const auto open = [&](std::int64_t x, std::int64_t y) {
        return x >= 0 && y >= 0 && x < width && y < height &&
               passable[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    };
    const auto node = [&](std::int64_t x, std::int64_t y) {
        return NodeId{
            map.node_of_cell_[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)]};
    };

    // Fixed neighbour order (up, right, down, left, then diagonals clockwise
    // from up-right) gives every node a deterministic edge order.
    constexpr std::array<std::array<int, 2>, 4> kCardinal{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    constexpr std::array<std::array<int, 2>, 4> kDiagonal{{{1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};

    for (const GridCell cell : map.cell_of_node_) {
        const std::int64_t x = cell.x;
        const std::int64_t y = cell.y;
        for (const auto& [dx, dy] : kCardinal) {
            if (open(x + dx, y + dy)) {
                builder.addEdge(node(x, y), node(x + dx, y + dy), options.cardinal_cost);
            }
        }
        if (options.connectivity == GridConnectivity::Eight) {
            for (const auto& [dx, dy] : kDiagonal) {
                // No corner cutting: both orthogonal cells must be passable.
                if (open(x + dx, y + dy) && open(x + dx, y) && open(x, y + dy)) {
                    builder.addEdge(node(x, y), node(x + dx, y + dy), options.diagonal_cost);
                }
            }
        }
    }

    auto graph = std::move(builder).build();
    if (!graph) {
        // Cannot happen for a well-formed grid; reported rather than asserted.
        return err(LoadError{0, "internal error building grid graph: " + graph.error().message});
    }
    map.graph_ = std::move(graph).value();
    return map;
}

Result<GridMap, LoadError> GridMap::fromRows(const std::vector<std::string>& rows,
                                             GridOptions options) {
    const std::size_t height = rows.size();
    const std::size_t width = rows.empty() ? 0 : rows.front().size();
    if (height > kMaxDimension || width > kMaxDimension) {
        return err(LoadError{0, "grid is too large"});
    }
    std::vector<bool> passable;
    passable.reserve(width * height);
    for (std::size_t y = 0; y < height; ++y) {
        if (rows[y].size() != width) {
            return err(LoadError{y + 1, "row has " + std::to_string(rows[y].size()) +
                                            " cells, expected " + std::to_string(width)});
        }
        for (const char c : rows[y]) {
            passable.push_back(c == '.');
        }
    }
    return build(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                 std::move(passable), options);
}

Result<GridMap, LoadError> GridMap::parseMovingAi(std::string_view text, GridOptions options) {
    const std::vector<Line> lines = splitLines(text);
    std::size_t index = 0;

    const auto next_header = [&]() -> const Line* {
        while (index < lines.size() && trim(lines[index].text).empty()) {
            ++index;
        }
        return index < lines.size() ? &lines[index++] : nullptr;
    };

    const Line* type = next_header();
    if (type == nullptr || trim(type->text).substr(0, 5) != "type ") {
        return err(LoadError{type != nullptr ? type->number : lines.size() + 1,
                             "expected 'type <name>' as the first line"});
    }

    std::optional<std::uint32_t> height;
    std::optional<std::uint32_t> width;
    while (true) {
        const Line* line = next_header();
        if (line == nullptr) {
            return err(LoadError{lines.size() + 1, "missing 'map' line"});
        }
        const std::string_view content = trim(line->text);
        if (content == "map") {
            break;
        }
        const std::size_t space = content.find_first_of(" \t");
        const std::string_view key = content.substr(0, space);
        const std::string_view value =
            space == std::string_view::npos ? std::string_view{} : trim(content.substr(space));
        if (key != "height" && key != "width") {
            return err(
                LoadError{line->number, "unexpected header line '" + std::string(content) + "'"});
        }
        std::optional<std::uint32_t>& slot = key == "height" ? height : width;
        if (slot) {
            return err(LoadError{line->number, "duplicate '" + std::string(key) + "' line"});
        }
        slot = parseDimension(value, kMaxDimension);
        if (!slot) {
            return err(LoadError{line->number, "invalid " + std::string(key) + " '" +
                                                   std::string(value) + "' (expected 1.." +
                                                   std::to_string(kMaxDimension) + ")"});
        }
    }
    if (!height || !width) {
        return err(
            LoadError{lines[index - 1].number,
                      std::string("missing '") + (height ? "width" : "height") + "' before 'map'"});
    }

    std::vector<bool> passable;
    passable.reserve(static_cast<std::size_t>(*width) * *height);
    for (std::uint32_t y = 0; y < *height; ++y) {
        if (index >= lines.size()) {
            return err(LoadError{lines.size() + 1, "expected " + std::to_string(*height) +
                                                       " map rows, found " + std::to_string(y)});
        }
        const Line& row = lines[index++];
        if (row.text.size() != *width) {
            return err(LoadError{row.number, "row has " + std::to_string(row.text.size()) +
                                                 " cells, expected " + std::to_string(*width)});
        }
        for (std::size_t x = 0; x < row.text.size(); ++x) {
            const char c = row.text[x];
            if (isMovingAiPassable(c)) {
                passable.push_back(true);
            } else if (isMovingAiBlocked(c)) {
                passable.push_back(false);
            } else {
                return err(LoadError{row.number, "unknown terrain '" + std::string(1, c) +
                                                     "' in column " + std::to_string(x + 1)});
            }
        }
    }
    for (; index < lines.size(); ++index) {
        if (!trim(lines[index].text).empty()) {
            return err(LoadError{lines[index].number, "unexpected content after the map rows"});
        }
    }

    return build(*width, *height, std::move(passable), options);
}

Result<GridMap, LoadError> GridMap::loadMovingAi(const std::filesystem::path& path,
                                                 GridOptions options) {
    const std::ifstream in(path, std::ios::binary);
    if (!in) {
        return err(LoadError{0, "cannot open '" + path.string() + "'"});
    }
    std::ostringstream content;
    content << in.rdbuf();
    if (in.bad()) {
        return err(LoadError{0, "error reading '" + path.string() + "'"});
    }
    auto map = parseMovingAi(content.str(), options);
    if (!map) {
        LoadError error = std::move(map).error();
        error.message = path.filename().string() + ": " + error.message;
        return err(std::move(error));
    }
    return map;
}

bool GridMap::passable(std::uint32_t x, std::uint32_t y) const noexcept {
    return nodeAt(x, y).has_value();
}

std::optional<NodeId> GridMap::nodeAt(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= width_ || y >= height_) {
        return std::nullopt;
    }
    const NodeId::rep_type node = node_of_cell_[static_cast<std::size_t>(y) * width_ + x];
    if (node == NodeId::kInvalidValue) {
        return std::nullopt;
    }
    return NodeId{node};
}

GridCell GridMap::cellOf(NodeId node) const {
    if (!node.valid() || node.value() >= cell_of_node_.size()) {
        throw std::out_of_range("GridMap: unknown " + toString(node));
    }
    return cell_of_node_[node.value()];
}

}  // namespace mapc
