#include "mapc/planning/best_first_search.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace mapc {

namespace {

constexpr std::uint32_t kMaxWeight = 1000;
constexpr std::uint32_t kWeightResolution = 1000;

/// Heap order: the entry that compares "greater" is expanded later.
struct ExpandLater {
    template <typename Entry>
    bool operator()(const Entry& a, const Entry& b) const noexcept {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        if (a.g != b.g) {
            return a.g < b.g;  // prefer deeper nodes on equal f
        }
        return a.node > b.node;
    }
};

}  // namespace

// --- HeuristicWeight -----------------------------------------------------------

HeuristicWeight HeuristicWeight::fraction(std::uint32_t numerator, std::uint32_t denominator) {
    if (numerator == 0 || denominator == 0) {
        throw std::invalid_argument("HeuristicWeight: numerator and denominator must be positive");
    }
    if (numerator < denominator || static_cast<std::uint64_t>(numerator) >
                                       static_cast<std::uint64_t>(kMaxWeight) * denominator) {
        throw std::invalid_argument("HeuristicWeight: weight must be within [1, 1000]");
    }
    const std::uint32_t divisor = std::gcd(numerator, denominator);
    return {numerator / divisor, denominator / divisor};
}

HeuristicWeight HeuristicWeight::fromDouble(double weight) {
    if (std::isnan(weight) || weight < 1.0 || weight > static_cast<double>(kMaxWeight)) {
        throw std::invalid_argument("HeuristicWeight: weight must be within [1, 1000]");
    }
    const auto scaled =
        static_cast<std::uint32_t>(std::llround(weight * static_cast<double>(kWeightResolution)));
    return fraction(scaled, kWeightResolution);
}

// --- BestFirstSearch -----------------------------------------------------------

BestFirstSearch::BestFirstSearch(std::shared_ptr<const Heuristic> heuristic, HeuristicWeight weight,
                                 std::string name)
    : heuristic_(std::move(heuristic)), weight_(weight), name_(std::move(name)) {
    if (!heuristic_) {
        throw std::invalid_argument("BestFirstSearch: heuristic must not be null");
    }
}

void BestFirstSearch::prepare(std::size_t node_count) {
    if (g_.size() != node_count) {
        seen_stamp_.assign(node_count, 0);
        closed_stamp_.assign(node_count, 0);
        g_.assign(node_count, 0);
        parent_edge_.assign(node_count, EdgeId::kInvalidValue);
        generation_ = 0;
    }
    ++generation_;
    if (generation_ == 0) {
        // The stamp wrapped around (after 2^32 queries): clear and restart.
        std::fill(seen_stamp_.begin(), seen_stamp_.end(), 0U);
        std::fill(closed_stamp_.begin(), closed_stamp_.end(), 0U);
        generation_ = 1;
    }
    open_.clear();
}

UInt128 BestFirstSearch::priority(Duration::rep g, Duration h) const noexcept {
    // g is never negative (costs are positive); a heuristic is a lower bound
    // on a non-negative cost, so a negative estimate is clamped to zero. Each
    // product is below 2^95, so the sum is exact.
    const Duration::rep estimate = std::max<Duration::rep>(h.count(), 0);
    return UInt128::multiply(static_cast<std::uint64_t>(g), weight_.denominator()) +
           UInt128::multiply(static_cast<std::uint64_t>(estimate), weight_.numerator());
}

void BestFirstSearch::push(OpenEntry entry) {
    open_.push_back(entry);
    std::push_heap(open_.begin(), open_.end(), ExpandLater{});
}

Path BestFirstSearch::reconstruct(const GraphView& view, const PlanRequest& request) const {
    std::vector<EdgeId> edges;
    NodeId::rep_type node = request.goal.value();
    while (node != request.start.value()) {
        const EdgeId edge{parent_edge_[node]};
        edges.push_back(edge);
        node = view.graph().source(edge).value();
    }
    std::reverse(edges.begin(), edges.end());

    Path path;
    path.steps.reserve(edges.size() + 1);
    Tick time = request.start_time;
    path.steps.push_back(PathStep{request.start, time});
    for (const EdgeId edge : edges) {
        time += view.cost(edge);
        path.steps.push_back(PathStep{view.graph().target(edge), time});
    }
    path.cost = Duration{g_[request.goal.value()]};
    return path;
}

PlanResult BestFirstSearch::plan(const GraphView& view, const PlanRequest& request) {
    const auto started = std::chrono::steady_clock::now();
    const Graph& graph = view.graph();
    if (!heuristic_->compatibleWith(graph)) {
        throw std::invalid_argument("BestFirstSearch: the heuristic was made for another graph");
    }

    SearchStats stats;
    const auto finish = [&](Result<Path, PlanFailure> outcome) {
        stats.elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
        return PlanResult{std::move(outcome), stats};
    };

    if (!graph.contains(request.start) || !graph.contains(request.goal)) {
        return finish(err(PlanFailure::InvalidRequest));
    }

    prepare(graph.nodeCount());
    const NodeId::rep_type start = request.start.value();
    seen_stamp_[start] = generation_;
    g_[start] = 0;
    parent_edge_[start] = EdgeId::kInvalidValue;
    push(OpenEntry{priority(0, heuristic_->estimate(request.start, request.goal)), 0, start});
    stats.generated = 1;
    stats.max_open = 1;

    const std::uint64_t max_expansions = request.limits.max_expansions;
    while (!open_.empty()) {
        std::pop_heap(open_.begin(), open_.end(), ExpandLater{});
        const OpenEntry top = open_.back();
        open_.pop_back();
        if (closed(top.node) || top.g != g_[top.node]) {
            continue;  // stale entry superseded by a cheaper one
        }
        if (top.node == request.goal.value()) {
            return finish(reconstruct(view, request));
        }
        if (max_expansions != 0 && stats.expanded >= max_expansions) {
            return finish(err(PlanFailure::LimitReached));
        }

        closed_stamp_[top.node] = generation_;
        ++stats.expanded;
        for (const EdgeId edge : graph.outEdges(NodeId{top.node})) {
            if (!view.traversable(edge)) {
                continue;
            }
            const NodeId next = graph.target(edge);
            const NodeId::rep_type v = next.value();
            if (closed(v)) {
                continue;
            }
            const Duration::rep g = detail::saturatingAdd(top.g, view.cost(edge).count());
            if (seen(v) && g >= g_[v]) {
                continue;
            }
            seen_stamp_[v] = generation_;
            g_[v] = g;
            parent_edge_[v] = edge.value();
            push(OpenEntry{priority(g, heuristic_->estimate(next, request.goal)), g, v});
            ++stats.generated;
        }
        stats.max_open = std::max<std::uint64_t>(stats.max_open, open_.size());
    }
    return finish(err(PlanFailure::NoPath));
}

// --- Concrete planners -------------------------------------------------------

DijkstraPlanner::DijkstraPlanner()
    : BestFirstSearch(std::make_shared<ZeroHeuristic>(), HeuristicWeight::one(), "dijkstra") {}

AStarPlanner::AStarPlanner(std::shared_ptr<const Heuristic> heuristic)
    : BestFirstSearch(std::move(heuristic), HeuristicWeight::one(), "astar") {}

WeightedAStarPlanner::WeightedAStarPlanner(std::shared_ptr<const Heuristic> heuristic,
                                           HeuristicWeight weight)
    : BestFirstSearch(std::move(heuristic), weight, "wastar") {}

}  // namespace mapc
