# Roadmap

Each milestone ends in a state that is **demoable and green in CI**, so the
project is presentable even if work stops partway. Section references point to
[DESIGN.md](DESIGN.md).

Effort estimates assume part-time work of about 10–15 h/week.

| # | Milestone | Est. | Demo at the end |
|---|---|---|---|
| M0 | Foundations | 0.5 wk | `ctest` passes under all presets |
| M1 | Graph and single-agent planning | 1.5 wk | `mapc_plan map.map sx sy gx gy` prints a path; A\* vs Dijkstra benchmark |
| M2 | Allocation | 1 wk | Allocator comparison table on random instances |
| M3 | Behavior tree engine | 1 wk | Unit-tested BT engine and a toy agent ticking a tree |
| M4 | Simulation loop with in-process coordination | 2 wk | **Warehouse with 20 agents, single-threaded, ASCII render** |
| M5 | Multi-agent planning (space-time A\*, reservations) | 1.5 wk | Zero collisions on the dense warehouse map, validated |
| M6 | Thread pool and parallel planning (OCC) | 1.5 wk | Speedup curve, TSan clean, determinism test |
| M7 | Dynamics, faults and recovery | 1.5 wk | Scripted fault timeline with recovery shown in the trace |
| M8 | Constraints and resources | 1 wk | Docks, chargers, no-go zones and deadlines in the warehouse |
| M9 | Extra scenarios | 1 wk | Delivery fleet and drone search with no core changes |
| M10 | ROS 2 adapter | 2 wk | Multi-process run; killing an agent process triggers recovery |
| M11 | Polish | 1 wk | README with GIF, benchmark report, architecture write-up |
| S | Stretch | — | CBS, auction allocator, web viewer, C++20 migration |

Total for M0–M11 is about 16 weeks part-time. The minimum version that is
**strong enough to show** is M0–M7, which takes about 10 weeks.

---

## M0 — Foundations

- CMake project, `core` library target, `tests/` and `bench/` (`apps/` arrives
  with its first executable in M1)
- CMake presets: `debug`, `release`, `asan-ubsan`, `tsan`, `coverage`, `tidy`;
  warnings as errors
- FetchContent dependencies, pinned by SHA-256: GoogleTest and Google
  Benchmark (yaml-cpp is added in M1, where the first loader needs it)
- `.clang-format`, `.clang-tidy`, `.editorconfig`
- GitHub Actions: GCC and Clang matrix, sanitizer jobs, clang-tidy,
  clang-format, coverage gate, no-ROS-in-core check
- Install rules and a `find_package(mapc)` package config
- `common/`: strong IDs, `Tick`, a `Result<T, E>`-style return type, seeded RNG
  wrapper, minimal logging

**Done when:** a trivial test passes in CI under every preset, and the README
explains how to build.

## M1 — Graph and single-agent planning (§4.2, §5.1–5.2)

- `Graph` (CSR), `GraphBuilder`, `GraphOverlay`
- Loaders: MovingAI `.map` grids, and a YAML node/edge list in a separate
  `mapc_io` library (adds yaml-cpp, which `mapc_core` must not depend on)
- `Planner` interface, `PlanResult`, search statistics
- Dijkstra, A\*, weighted A\*, plus grid and Euclidean heuristics
- `mapc_plan` CLI

**Done when:**
- A property test shows A\* cost equals Dijkstra cost on 1,000 random graphs.
- A weighted-A\* test asserts the `w × optimal` bound.
- A benchmark covers MovingAI maps such as `den312d` and `warehouse-10-20-10-2-1`
  (downloaded at a pinned hash, not committed).

## M2 — Task allocation (§6.2)

- `Task`, `AgentSnapshot`, `CostModel` with a cached distance oracle
- Allocators: greedy nearest, cost-based greedy, Hungarian (rectangular,
  maximum number of assignments first, then minimum cost)
- Capability filtering through infinite (infeasible) costs

**Done when:**
- Hungarian matches brute force for n ≤ 7 in a property test.
- Rectangular cases (more tasks than agents, and the reverse) are tested.
- A bench table compares total cost and runtime for the three strategies
  (random instances on the MovingAI warehouse map, fixed seed).

## M3 — Behavior tree engine (§7)

- `Node`, `Status`, `Blackboard`
- `Sequence`, `Fallback`, `ReactiveSequence`, `ReactiveFallback`, `Parallel`
- Decorators: `Inverter`, `Retry`, `Timeout`, `RateLimit`
- Builder DSL in C++ for composing trees, plus an ASCII tree dump for debugging

**Done when:**
- Each node type has tests for status semantics, including how `Running`
  propagates and how reactive nodes halt running children.
- A toy agent tree runs against a fake world.

## M4 — Simulation loop and in-process coordination (§4.4–4.5, §6.1, §6.4, §9.1)

This is the first end-to-end milestone and the most important one.

- `WorldState`, `WorldSnapshot` with versioning, and `Simulator::step()` as
  specified in the design
- Message types and `Transport` with `InProcessTransport`, using deterministic
  delivery
- `Coordinator`: task queue, allocation trigger, naive path commit (no
  reservations yet)
- `MobileAgent` and `AgentController` running the default BT, planning
  synchronously for now
- Warehouse scenario YAML, scenario loader, task generator
- `mapc_sim`: writes `trace.json` and `metrics.json` and can render ASCII live

**Done when:**
- 20 agents complete tasks in the warehouse.
- Metrics report throughput and latency.
- Collisions are allowed at this point but counted. The count gives M5 its
  baseline.

## M5 — Multi-agent planning (§5.3)

- `ReservationTable` with vertex, edge and parking reservations
- Space-time A\* with wait actions, a horizon limit and swap-conflict checks
- Prioritized planning inside the coordinator's commit path
- An independent `PathSetValidator`, used by tests and in debug builds

**Done when:**
- The validator reports zero vertex and swap conflicts across 100 seeded
  warehouse runs.
- Prioritized-planning failures are counted and reported.

## M6 — Thread pool and parallel planning (§6.3, §8)

- `ThreadPool` with futures, cancellation tokens and RAII shutdown
- `PlanningExecutor` with a per-worker `Planner` instance from a factory
- Asynchronous `RequestPlan` BT action that returns `Running` until its result
  arrives
- OCC commit protocol: versioned proposals, recheck, reject and replan, with a
  retry cap
- Canonical-order application of results at tick boundaries

**Done when:**
- The TSan preset is clean.
- The determinism test produces byte-identical traces for 1 and 8 workers.
- A speedup chart covers 1, 2, 4 and 8 workers on a sparse and a dense map.
- The OCC rejection rate is reported for each.

## M7 — Dynamics, faults and recovery (§9.2)

- `FaultInjector` with scripted timelines and seeded random faults
- Every fault in the design table, including message delay and drop in
  `InProcessTransport`
- `HealthMonitor`: heartbeat timeout, task re-queue, reservation and lease
  release
- `ReplanRequired` flow and BT `Recover` branch
- Sequence numbers and epochs on messages, so stale ones are ignored

**Done when:**
- Each fault type has an integration test asserting recovery. Every task
  eventually reaches Done or an explicit Failed or Cancelled state, and no
  agent deadlocks.
- A demo trace shows a blocked aisle followed by a reroute.

> **Checkpoint:** M0–M7 is a complete, defensible project. Write the README
> and record a demo GIF here before continuing.

## M8 — Constraints and resources (§5.4, §6.1)

- Search constraints: `ForbiddenEdge`, `ForbiddenNode` (time-windowed),
  `NodeCapacity`, deadline pruning
- Validator interface with factory registration from YAML
- `ResourceManager` with leases and expiry: docks as capacity-1 resources,
  chargers as leased resources
- Allocator awareness of deadlines and requirements

**Done when:**
- The warehouse uses docks and chargers.
- Tests show each constraint type being respected.
- The deadline-miss metric is reported.

## M9 — Additional scenarios (§10)

- Delivery fleet: road graph, Euclidean heuristic, traffic as cost changes,
  deadlines
- Drone search: no-fly zones, runtime-generated search tasks, `ReportTarget`
  leaf

**Done when:** both scenarios run in CI and the diff for this milestone touches
only `scenarios/` (plus any core bug fixes, each justified separately).

## M10 — ROS 2 adapter (§11)

- `mapc_msgs` package
- `Ros2Transport`
- Coordinator, agent and world nodes
- Launch file and QoS profiles
- CI job in the `ros:jazzy` container

**Done when:**
- The warehouse runs with 1 coordinator, 1 world node and 10 agent processes.
- Completed-task counts are comparable to the in-process run.
- `kill -9` on one agent process triggers recovery and its task is reassigned.

## M11 — Polish

- README: problem statement, architecture diagram, GIF, quick start, results
  table
- `docs/BENCHMARKS.md`: planner and allocator comparisons, speedup curves, OCC
  analysis, with honest discussion of where things degrade
- Short design-decision log covering the questions in DESIGN.md §14
- Coverage badge and CI badge

## Stretch

- CBS for small agent groups where prioritized planning fails
- Auction-based allocation over the transport, so it works in distributed mode
- Web trace viewer (static HTML reading `trace.json`)
- Lifelong MAPF benchmark comparison against published numbers
- C++20 migration: `std::span`, `std::jthread` and `std::stop_token`, concepts
  for strategy interfaces

---

## Working rules

- Keep the public repo neutral. Docs describe the engineering problem, not any
  specific employer or job posting.
- Every milestone adds tests before the demo, and CI stays green on `main`.
- Never put a ROS include under `core/`. CI enforces this with a grep check.
- Every benchmark result in docs must be reproducible from a committed script
  and seed.
