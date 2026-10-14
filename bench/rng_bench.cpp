#include "mapc/common/rng.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace {

void BM_RngNextU64(benchmark::State& state) {
    mapc::Rng rng(1);
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng.nextU64());
    }
}
BENCHMARK(BM_RngNextU64);

void BM_StdMt19937_64(benchmark::State& state) {
    std::mt19937_64 rng(1);
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng());
    }
}
BENCHMARK(BM_StdMt19937_64);

void BM_RngUniformBelow(benchmark::State& state) {
    mapc::Rng rng(1);
    const auto bound = static_cast<std::uint64_t>(state.range(0));
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng.uniformBelow(bound));
    }
}
BENCHMARK(BM_RngUniformBelow)->Arg(10)->Arg(1000003)->Arg(int64_t{1} << 62);

void BM_RngUniformReal(benchmark::State& state) {
    mapc::Rng rng(1);
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng.uniformReal());
    }
}
BENCHMARK(BM_RngUniformReal);

void BM_RngShuffle(benchmark::State& state) {
    mapc::Rng rng(1);
    std::vector<int> values(static_cast<std::size_t>(state.range(0)));
    std::iota(values.begin(), values.end(), 0);
    for (auto _ : state) {
        rng.shuffle(values.begin(), values.end());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_RngShuffle)->Arg(64)->Arg(4096);

}  // namespace
