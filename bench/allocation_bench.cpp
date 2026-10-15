// Task allocator comparison on random instances in the MovingAI warehouse map
// (warehouse-10-20-10-2-1, 4-connected, unit costs).
//
// For every size, 8 instances are drawn with a fixed seed: agents on random
// cells, tasks with a random source and destination, all in the largest
// connected region. A quarter of the tasks need a capability that only half
// of the agents have, so capability filtering is exercised. Costs come from
// TravelCostModel (approach + delivery travel time) and are computed once,
// outside the timed loop; one iteration solves all 8 cost matrices, so the
// Time column is per batch and "time/instance" is per instance.
//
// Counters, averaged per instance:
//   assigned   pairs assigned
//   cost       total cost of the assignment (ticks)
//   gap_%      how much more the assignment costs than the Hungarian one
//              (only meaningful when "assigned" matches)
// The */costs rows time building the 8 cost matrices from a cold distance
// oracle, which is what an allocation round pays before any strategy runs.

#include "mapc/allocation/cost_model.hpp"
#include "mapc/allocation/distance_oracle.hpp"
#include "mapc/allocation/task_allocator.hpp"
#include "mapc/common/rng.hpp"
#include "mapc/graph/grid_map.hpp"

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

constexpr std::uint64_t kSeed = 2026;
constexpr std::size_t kInstancesPerSize = 8;

struct Instance {
    std::vector<mapc::AgentSnapshot> agents;
    std::vector<mapc::Task> tasks;
    mapc::CostMatrix costs;
};

struct SizeWorkload {
    std::string label;
    std::vector<Instance> instances;
    std::vector<double> optimal_cost;  // Hungarian total per instance
};

Instance randomInstance(mapc::Rng& rng, const std::vector<mapc::NodeId>& region,
                        std::size_t agent_count, std::size_t task_count) {
    const mapc::CapabilitySet special = mapc::CapabilitySet::of({0});
    const auto pick = [&] { return region[rng.uniformBelow(region.size())]; };
    Instance instance;
    for (std::size_t a = 0; a < agent_count; ++a) {
        instance.agents.push_back({mapc::AgentId{static_cast<mapc::AgentId::rep_type>(a)}, pick(),
                                   a % 2 == 0 ? special : mapc::CapabilitySet{},
                                   mapc::TaskId::invalid()});
    }
    for (std::size_t t = 0; t < task_count; ++t) {
        mapc::Task task;
        task.id = mapc::TaskId{static_cast<mapc::TaskId::rep_type>(t)};
        task.source = pick();
        task.destination = pick();
        if (t % 4 == 0) {
            task.requirements.capabilities = special;
        }
        instance.tasks.push_back(task);
    }
    return instance;
}

mapc::CostMatrix costsFor(const Instance& instance, const mapc::GraphView& view) {
    const mapc::TravelCostModel model(std::make_shared<mapc::DistanceOracle>(view));
    return mapc::CostMatrix::build(instance.agents, instance.tasks, model);
}

void runAllocator(benchmark::State& state, const SizeWorkload& workload,
                  const mapc::TaskAllocator& allocator) {
    std::uint64_t assigned = 0;
    double cost = 0;
    double gap = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < workload.instances.size(); ++i) {
            const mapc::CostMatrix& costs = workload.instances[i].costs;
            const std::vector<mapc::MatrixMatch> matches = allocator.solve(costs);
            benchmark::DoNotOptimize(matches.data());
            const auto total = static_cast<double>(mapc::totalCost(costs, matches));
            assigned += matches.size();
            cost += total;
            const double optimal = workload.optimal_cost[i];
            gap += optimal > 0 ? 100.0 * (total - optimal) / optimal : 0.0;
        }
    }
    const auto instances =
        static_cast<double>(state.iterations()) * static_cast<double>(workload.instances.size());
    state.counters["assigned"] = static_cast<double>(assigned) / instances;
    state.counters["cost"] = cost / instances;
    state.counters["gap_%"] = gap / instances;
    state.counters["time/instance"] =
        benchmark::Counter(instances, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

void runCostBuild(benchmark::State& state, const SizeWorkload& workload,
                  const mapc::GraphView& view) {
    for (auto _ : state) {
        for (const Instance& instance : workload.instances) {
            const mapc::CostMatrix costs = costsFor(instance, view);
            benchmark::DoNotOptimize(&costs);
        }
    }
    state.counters["time/instance"] = benchmark::Counter(
        static_cast<double>(state.iterations()) * static_cast<double>(workload.instances.size()),
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string map_name = "warehouse-10-20-10-2-1.map";
    auto loaded = mapc::GridMap::loadMovingAi(std::string(MAPC_MAPF_MAPS_DIR) + "/" + map_name);
    if (!loaded) {
        std::cerr << "skipping " << map_name << ": " << loaded.error().describe() << '\n';
        return 0;
    }
    const mapc::GridMap map = std::move(loaded).value();
    const mapc::GraphView view(map.graph());
    const std::vector<mapc::NodeId> region = mapc::bench::largestRegion(map.graph());

    static const mapc::GreedyNearestAllocator kNearest;
    static const mapc::CostGreedyAllocator kCostGreedy;
    static const mapc::HungarianAllocator kHungarian;
    const std::vector<const mapc::TaskAllocator*> allocators{&kNearest, &kCostGreedy, &kHungarian};

    const std::vector<std::pair<std::size_t, std::size_t>> sizes{{10, 10},   {50, 50},  {100, 100},
                                                                 {200, 200}, {20, 100}, {100, 20}};
    std::vector<std::unique_ptr<SizeWorkload>> workloads;
    mapc::Rng rng(kSeed);
    for (const auto& [agents, tasks] : sizes) {
        auto workload = std::make_unique<SizeWorkload>();
        workload->label = std::to_string(agents) + "x" + std::to_string(tasks);
        for (std::size_t i = 0; i < kInstancesPerSize; ++i) {
            Instance instance = randomInstance(rng, region, agents, tasks);
            instance.costs = costsFor(instance, view);
            workload->optimal_cost.push_back(static_cast<double>(
                mapc::totalCost(instance.costs, kHungarian.solve(instance.costs))));
            workload->instances.push_back(std::move(instance));
        }
        const SizeWorkload& w = *workload;
        for (const mapc::TaskAllocator* allocator : allocators) {
            benchmark::RegisterBenchmark(
                (w.label + "/" + std::string(allocator->name())).c_str(),
                [&w, allocator](benchmark::State& s) { runAllocator(s, w, *allocator); })
                ->Unit(benchmark::kMicrosecond);
        }
        benchmark::RegisterBenchmark((w.label + "/costs").c_str(), [&w,
                                                                    &view](benchmark::State& s) {
            runCostBuild(s, w, view);
        })->Unit(benchmark::kMicrosecond);
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
