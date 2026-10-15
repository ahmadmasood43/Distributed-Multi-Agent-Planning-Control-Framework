// Single-agent planner benchmark on MovingAI MAPF maps.
//
// For each map, 64 start/goal pairs are drawn with a fixed seed from the
// largest connected region (so every query has a path). One benchmark
// iteration solves all 64; time is per batch, counters are per query.

#include "mapc/common/rng.hpp"
#include "mapc/graph/grid_map.hpp"
#include "mapc/planning/best_first_search.hpp"
#include "mapc/planning/heuristics.hpp"

#include "map_workload.hpp"
#include <benchmark/benchmark.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef MAPC_MAPF_MAPS_DIR
#error "MAPC_MAPF_MAPS_DIR must point at the extracted MovingAI MAPF maps"
#endif

namespace {

using mapc::GraphView;
using mapc::NodeId;

constexpr std::uint64_t kSeed = 2026;
constexpr std::size_t kQueries = 64;

struct Workload {
    std::shared_ptr<const mapc::GridMap> map;
    std::shared_ptr<const mapc::Heuristic> heuristic;
    std::vector<std::pair<NodeId, NodeId>> queries;
};

std::unique_ptr<Workload> loadWorkload(const std::string& name) {
    auto loaded = mapc::GridMap::loadMovingAi(std::string(MAPC_MAPF_MAPS_DIR) + "/" + name);
    if (!loaded) {
        std::cerr << "skipping " << name << ": " << loaded.error().describe() << '\n';
        return nullptr;
    }
    auto workload = std::make_unique<Workload>();
    workload->map = std::make_shared<const mapc::GridMap>(std::move(loaded).value());
    workload->heuristic = std::make_shared<const mapc::GridHeuristic>(*workload->map);
    const std::vector<NodeId> region = mapc::bench::largestRegion(workload->map->graph());
    mapc::Rng rng(kSeed);
    for (std::size_t i = 0; i < kQueries; ++i) {
        workload->queries.emplace_back(region[rng.uniformBelow(region.size())],
                                       region[rng.uniformBelow(region.size())]);
    }
    return workload;
}

template <typename MakePlanner>
void runPlanner(benchmark::State& state, const Workload& workload, MakePlanner make) {
    // Each iteration solves the whole batch, so every planner is measured on
    // exactly the same queries and the per-query averages are comparable.
    auto planner = make(workload);
    const GraphView view(workload.map->graph());
    std::uint64_t expanded = 0;
    std::int64_t cost = 0;
    for (auto _ : state) {
        for (const auto& [start, goal] : workload.queries) {
            mapc::PlanRequest request;
            request.start = start;
            request.goal = goal;
            const mapc::PlanResult result = planner->plan(view, request);
            if (!result.found()) {
                state.SkipWithError("query without a path");
                return;
            }
            expanded += result.stats.expanded;
            cost += result.outcome->cost.count();
        }
    }
    const auto queries =
        static_cast<double>(state.iterations()) * static_cast<double>(workload.queries.size());
    state.SetItemsProcessed(static_cast<std::int64_t>(queries));
    state.counters["expanded/query"] = static_cast<double>(expanded) / queries;
    state.counters["cost/query"] = static_cast<double>(cost) / queries;
    state.counters["nodes"] = static_cast<double>(workload.map->graph().nodeCount());
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::unique_ptr<Workload>> workloads;
    for (const std::string name : {"den312d.map", "warehouse-10-20-10-2-1.map"}) {
        auto workload = loadWorkload(name);
        if (!workload) {
            continue;
        }
        const Workload& w = *workload;
        const std::string prefix = name.substr(0, name.find('.')) + "/";
        benchmark::RegisterBenchmark((prefix + "dijkstra").c_str(), [&w](benchmark::State& s) {
            runPlanner(s, w,
                       [](const Workload&) { return std::make_unique<mapc::DijkstraPlanner>(); });
        });
        benchmark::RegisterBenchmark((prefix + "astar").c_str(), [&w](benchmark::State& s) {
            runPlanner(s, w, [](const Workload& wl) {
                return std::make_unique<mapc::AStarPlanner>(wl.heuristic);
            });
        });
        for (const double weight : {1.5, 3.0}) {
            const std::string label = prefix + "wastar/w=" + std::to_string(weight).substr(0, 3);
            benchmark::RegisterBenchmark(label.c_str(), [&w, weight](benchmark::State& s) {
                runPlanner(s, w, [weight](const Workload& wl) {
                    return std::make_unique<mapc::WeightedAStarPlanner>(
                        wl.heuristic, mapc::HeuristicWeight::fromDouble(weight));
                });
            });
        }
        workloads.push_back(std::move(workload));
    }
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
