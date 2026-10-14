# Distributed Multi-Agent Planning & Control Framework

A modular C++17 framework for coordinating autonomous agents in dynamic environments. It provides reusable components for:

- Task allocation
- Graph-based path planning
- Behavior-tree control
- Resource coordination and conflict resolution
- Failure recovery

Planning runs concurrently on a thread pool. The same coordinator and agent logic runs in a single process or as separate ROS 2 processes.

Example scenarios: warehouse robots, a delivery fleet, and multi-drone search.

> Status: milestone M0 (foundations) complete. See the [roadmap](docs/ROADMAP.md).

- [Design](docs/DESIGN.md): architecture, interfaces, threading model, protocols
- [Roadmap](docs/ROADMAP.md): milestones with acceptance criteria

## Building

Requirements: CMake ≥ 3.21, Ninja and a C++17 compiler (tested with GCC 13, and with Clang 22 on libc++). GoogleTest and Google Benchmark are downloaded automatically at pinned versions.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

| Preset | Purpose |
|---|---|
| `debug` | Development build, warnings as errors |
| `release` | Optimised build; also builds `bench/mapc_micro_bench` |
| `asan-ubsan` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `tsan` | ThreadSanitizer |
| `coverage` | gcov instrumentation; report with `gcovr build/coverage` (settings in `gcovr.cfg`) |
| `tidy` | Runs clang-tidy on every first-party translation unit while compiling |

On Linux kernels with high mmap randomisation, ThreadSanitizer aborts with "unexpected memory mapping". Either lower the entropy once with `sudo sysctl vm.mmap_rnd_bits=28`, or run the tests with ASLR disabled:

```bash
setarch "$(uname -m)" -R ctest --preset tsan
```

### Using the library from another CMake project

```cmake
find_package(mapc 0.1 REQUIRED)   # after `cmake --install build/release --prefix <dir>`
target_link_libraries(my_app PRIVATE mapc::core)
```

`add_subdirectory()` also works; tests and benchmarks are then off by default.

## Layout

```
core/include/mapc/   public headers (namespace mapc)
core/src/            library sources
tests/unit/          GoogleTest unit tests
tests/cmake/         fixtures for build-rule tests
bench/               Google Benchmark micro-benchmarks
cmake/               warnings, sanitizers, coverage, install modules
tools/               repository checks (e.g. core must not depend on ROS)
docs/                design and roadmap
```

## Architecture rule: no ROS in the core

`core/` must build and run without ROS; ROS 2 lives only in a separate adapter. The rule is enforced in four layers:

1. **`tools/check_no_ros_in_core.cmake`** scans the sources. It is a C/C++ and CMake tokenizer, so comments and literals can neither hide a dependency nor fake one, and it reports the file and line of each violation. Besides `#include` directives and CMake commands, it flags ROS identifiers in code: ROS C API names, rosidl C names and ROS C++ namespaces. That covers ROS functions declared by hand, including in inline or template code that the library never instantiates. Besides `core/`, it scans the generated include directory after generation. It also scans every file a real `cmake --install` produces, staged through `DESTDIR` and checked whatever its name or destination, which is exactly what clients receive.
2. **`cmake/ArchitectureRules.cmake`** checks what CMake resolved. It runs at the end of configuration and again on the requirements as generated. The second run sees changes made later, for example by deferred calls. Both runs check `mapc_core` and everything it links:
   - only allowlisted targets, whose usage requirements are checked in turn, plus allowlisted plain link items that must never name a target;
   - no legacy `LINK_FLAGS` and no link directories;
   - no compile options that add include paths or libraries;
   - include directories, sources and precompiled headers must come from inside the repository.
3. **`tools/check_header_deps.cmake`** checks what the compiler actually read. After a Ninja build, it walks the build graph to every object file in `mapc_core`, including object-library members. It then examines the headers each one was compiled with, resolving symlinks, and runs the source scanner on every core-owned file the compiler read, whatever its extension. This catches any spelling of `#include`, including macros.
4. **`tools/check_core_symbols.cmake`** checks what the built library needs. It rejects these entries in `libmapc_core`:
   - undefined ROS functions or variables, including ones declared by hand without a header;
   - rosidl-generated C names and ROS C++ namespaces;
   - run-time loading (`dlopen`/`dlsym`);
   - for a shared build, any `DT_NEEDED` entry that isn't a toolchain runtime library.

**Reserved names.** Top-level ROS namespace names are reserved in core code. That covers names matching ROS package patterns, such as `rclcpp`, `rmw*`, `tf2*`, `*_msgs`, `*_srvs` and `*_interfaces`. A project-owned namespace with such a name is fine when nested in `mapc`. Declare it with the C++17 form `namespace mapc::agent_msgs { … }` and refer to it qualified, as in `mapc::agent_msgs::Status`; the scanner rejects the form `namespace mapc { namespace agent_msgs { … } }`.

**Scope of the guarantee.** It covers the core library, and the usage requirements it exports, as produced by this repository's build. A project that consumes mapc, through `add_subdirectory`, FetchContent or `find_package`, controls its own build. It gets the configure-time check as an early warning, but changes it makes to mapc's targets afterwards are its own responsibility. Also out of scope:
- changes to the enforcement code or allowlists themselves;
- swapping the compiler, linker or toolchain;
- global flag variables and environment settings (any ROS header or symbol they bring in is still caught by layers 3–4);
- generators other than Ninja (for layer 3);
- ROS identifiers assembled by macros (for example token pasting) inside header code that the core library itself never instantiates. Layers 3–4 see only code that is compiled into the library, and layer 1 sees identifiers only as written.

## What M0 provides

`mapc/common/` holds the vocabulary types the rest of the framework builds on:

- **`strong_id.hpp`**: `AgentId`, `TaskId`, `NodeId`, `EdgeId` and `ResourceId`. They are zero-overhead, mutually incompatible, hashable, and use an explicit invalid sentinel.
- **`time.hpp`**: `Tick` (a point on the simulation timeline) and `Duration` (a span). They are distinct types with saturating arithmetic.
- **`result.hpp`**: `Result<T, E>` and `Result<void, E>`, a C++17 subset of `std::expected` with `map`, `andThen` and `mapError`.
- **`rng.hpp`**: a deterministic xoshiro256** generator with its own distributions. The same seed gives the same sequence on libstdc++, libc++ and MSVC. `derive(key)` creates independent per-agent streams.
- **`logging.hpp`**: a thread-safe leveled logger with a pluggable sink. Disabled statements are not evaluated, and the level expression is evaluated exactly once. Sink failures never reach the caller: exceptions are swallowed, re-entrant logging is dropped, and a sink may replace itself or log from its destructor without deadlocking. Every dropped record is counted. An exception thrown while formatting a message, for example by a user `operator<<`, propagates like any other expression. The incomplete record is discarded.
