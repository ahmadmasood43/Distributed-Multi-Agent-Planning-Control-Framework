#pragma once

#include "mapc/common/uint128.hpp"
#include "mapc/planning/heuristics.hpp"
#include "mapc/planning/planner.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mapc {

/// The heuristic weight of weighted A*, stored as an exact reduced fraction
/// so that priorities are computed in integer arithmetic (deterministic on
/// every platform, unlike floating-point products).
class HeuristicWeight {
public:
    /// Weight 1 (plain A*).
    [[nodiscard]] static constexpr HeuristicWeight one() noexcept { return {1, 1}; }

    /// numerator / denominator. Throws std::invalid_argument unless
    /// 1 <= weight <= 1000 and both are positive.
    [[nodiscard]] static HeuristicWeight fraction(std::uint32_t numerator,
                                                  std::uint32_t denominator);

    /// Rounds to the nearest 1/1000. Throws std::invalid_argument for NaN or
    /// values outside [1, 1000].
    [[nodiscard]] static HeuristicWeight fromDouble(double weight);

    [[nodiscard]] constexpr std::uint32_t numerator() const noexcept { return numerator_; }
    [[nodiscard]] constexpr std::uint32_t denominator() const noexcept { return denominator_; }
    [[nodiscard]] double value() const noexcept {
        return static_cast<double>(numerator_) / static_cast<double>(denominator_);
    }

    friend constexpr bool operator==(HeuristicWeight a, HeuristicWeight b) noexcept {
        return a.numerator_ == b.numerator_ && a.denominator_ == b.denominator_;
    }
    friend constexpr bool operator!=(HeuristicWeight a, HeuristicWeight b) noexcept {
        return !(a == b);
    }

private:
    constexpr HeuristicWeight(std::uint32_t numerator, std::uint32_t denominator) noexcept
        : numerator_(numerator), denominator_(denominator) {}

    std::uint32_t numerator_;
    std::uint32_t denominator_;
};

/// Best-first graph search ordered by f = g + weight * h, without re-opening
/// closed nodes.
///
/// With a consistent heuristic and weight 1 this is A* and returns optimal
/// paths; with the zero heuristic it is Dijkstra's algorithm; with weight
/// w > 1 the path cost is at most w times the optimum.
///
/// Determinism: ties are broken by larger g (deeper first), then by smaller
/// node id, and edges are relaxed in the graph's edge order, so the same
/// query always returns the same path.
///
/// Memory is kept between queries (per-node arrays reset in O(1) with a
/// generation stamp), so an instance is not thread-safe.
class BestFirstSearch : public Planner {
public:
    BestFirstSearch(std::shared_ptr<const Heuristic> heuristic, HeuristicWeight weight,
                    std::string name);

    /// Throws std::invalid_argument if the heuristic was made for another
    /// graph. Unknown start/goal nodes give PlanFailure::InvalidRequest.
    [[nodiscard]] PlanResult plan(const GraphView& view, const PlanRequest& request) override;

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] const Heuristic& heuristic() const noexcept { return *heuristic_; }
    [[nodiscard]] HeuristicWeight weight() const noexcept { return weight_; }

private:
    struct OpenEntry {
        // Exact priority g * denominator + numerator * h. Both products can
        // exceed 64 bits for large costs or fine-grained weights, and a
        // saturated priority would break the weighted-A* bound.
        UInt128 f;
        Duration::rep g = 0;
        NodeId::rep_type node = 0;
    };

    void prepare(std::size_t node_count);
    [[nodiscard]] bool seen(NodeId::rep_type node) const noexcept {
        return seen_stamp_[node] == generation_;
    }
    [[nodiscard]] bool closed(NodeId::rep_type node) const noexcept {
        return closed_stamp_[node] == generation_;
    }
    [[nodiscard]] UInt128 priority(Duration::rep g, Duration h) const noexcept;
    void push(OpenEntry entry);
    [[nodiscard]] Path reconstruct(const GraphView& view, const PlanRequest& request) const;

    std::shared_ptr<const Heuristic> heuristic_;
    HeuristicWeight weight_;
    std::string name_;

    std::uint32_t generation_ = 0;
    std::vector<std::uint32_t> seen_stamp_;
    std::vector<std::uint32_t> closed_stamp_;
    std::vector<Duration::rep> g_;
    std::vector<EdgeId::rep_type> parent_edge_;
    std::vector<OpenEntry> open_;
};

/// Dijkstra's algorithm (best-first search with the zero heuristic).
class DijkstraPlanner final : public BestFirstSearch {
public:
    DijkstraPlanner();
};

/// A* with the given heuristic (optimal for consistent heuristics).
class AStarPlanner final : public BestFirstSearch {
public:
    explicit AStarPlanner(std::shared_ptr<const Heuristic> heuristic);
};

/// Weighted A*: cost at most `weight` times optimal, usually far fewer
/// expansions than A*.
class WeightedAStarPlanner final : public BestFirstSearch {
public:
    WeightedAStarPlanner(std::shared_ptr<const Heuristic> heuristic, HeuristicWeight weight);
};

}  // namespace mapc
