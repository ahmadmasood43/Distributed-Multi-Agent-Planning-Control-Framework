#pragma once

#include <cmath>
#include <ostream>

namespace mapc {

/// A point in the plane, used for node positions and distance heuristics.
/// Units are whatever the map uses (cells for grids, metres for road maps).
struct Point2 {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(Point2 a, Point2 b) noexcept { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(Point2 a, Point2 b) noexcept { return !(a == b); }

    friend std::ostream& operator<<(std::ostream& os, Point2 p) {
        return os << '(' << p.x << ", " << p.y << ')';
    }
};

/// Euclidean distance between two points.
[[nodiscard]] inline double distance(Point2 a, Point2 b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

/// True if both coordinates are finite (not NaN or infinite).
[[nodiscard]] inline bool isFinite(Point2 p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

}  // namespace mapc
