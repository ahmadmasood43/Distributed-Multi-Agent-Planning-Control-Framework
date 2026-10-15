include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
    URL_HASH SHA256=32131c08ee31eeff2c8968d7e874f3cb648034377dfc32a4c377fa8796d84981)
FetchContent_MakeAvailable(benchmark)

add_executable(mapc_micro_bench
    rng_bench.cpp)

target_link_libraries(mapc_micro_bench PRIVATE
    mapc::core
    mapc::warnings
    benchmark::benchmark_main)

# MovingAI MAPF benchmark maps (Sturtevant, "Benchmarks for Grid-Based
# Pathfinding", https://movingai.com/benchmarks/mapf/). Downloaded at a pinned
# hash rather than committed to the repository.
FetchContent_Declare(mapf_maps
    URL https://movingai.com/benchmarks/mapf/mapf-map.zip
    URL_HASH SHA256=9da2e4c5ce03aa4e063b3a283ce874590b36cc4f31a297fe7ecb00d105abf288)
FetchContent_MakeAvailable(mapf_maps)

add_executable(mapc_planning_bench planning_bench.cpp)
target_link_libraries(mapc_planning_bench PRIVATE
    mapc::core
    mapc::warnings
    benchmark::benchmark)
target_compile_definitions(mapc_planning_bench PRIVATE
    MAPC_MAPF_MAPS_DIR="${mapf_maps_SOURCE_DIR}")
