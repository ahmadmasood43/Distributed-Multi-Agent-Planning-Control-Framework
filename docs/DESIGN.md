# Design: Distributed Multi-Agent Planning & Control Framework

Status: draft · Owner: @ahmad-masood-dev · Last updated: 2026-10-13

## 1. Purpose

A modular C++17 framework for coordinating many autonomous agents that share a
dynamic environment. It provides reusable building blocks for task allocation,
graph-based path planning, behavior control, resource coordination, conflict
resolution and failure recovery. Scenarios such as warehouse robots, delivery
fleets and drone search are built on top of one core engine.

### Goals

- **Domain-agnostic core.** The core has no warehouse, road or drone concepts.
  Agents, tasks, locations and resources are generic, and scenarios are
  configuration plus a small amount of glue code.
- **Pluggable strategies.** Planners, allocators and constraints sit behind
  narrow interfaces and are selected at runtime from scenario config.
- **Correct concurrency.** Planning runs on a thread pool. The design makes data
  races structurally hard rather than relying on careful locking, and the test
  suite runs clean under ASan, UBSan and TSan.
- **Deterministic simulation.** With the same seed and config, a run produces the
  same trace. Tests and benchmarks depend on this.
- **Transport independence.** In-process and multi-process (ROS 2) deployments
  share the same coordinator and agent logic. Only the transport adapter changes.

### Non-goals

- Continuous-space motion planning, kinematics or physics. Agents move on a
  graph in discrete time.
- A general-purpose constraint solver. Constraints are a typed API that
  planners and the coordinator understand. There is no CSP/SAT backend.
- Optimal multi-agent pathfinding at scale. Prioritized planning is the baseline
  and CBS is a stretch goal (see §5.3).
- A GUI. Output is a JSON trace, and visualization is a separate, optional tool.

## 2. Architecture overview

```
                 ┌─────────────────────────────────────────────┐
  scenario.yaml ─►              Scenario Loader                │
                 └──────────────────────┬──────────────────────┘
                                        │ builds
 ┌──────────────────────────────────────▼───────────────────────────────────────┐
 │                                   Coordinator                                │
 │  TaskQueue ─► TaskAllocator ─► ReservationTable ◄─ ConflictDetector          │
 │                    │              ResourceManager      HealthMonitor         │
 └────────────────────┼─────────────────────────▲───────────────────────────────┘
                      │ messages                │ messages
              ┌───────▼─────────────────────────┴──────┐
              │            Transport (interface)       │
              │   InProcessTransport | Ros2Transport   │
              └───────┬───────────────┬────────────────┘
                      │               │
               ┌──────▼─────┐   ┌─────▼──────┐
               │ Agent 1    │   │ Agent N    │      PlanningExecutor
               │ Controller │…  │ Controller │ ───► (thread pool)
               │  └ BT      │   │  └ BT      │      Planner instances
               └──────┬─────┘   └─────┬──────┘
                      │ reads snapshots│
              ┌───────▼────────────────▼───────┐
              │  World (versioned snapshots)   │ ◄── FaultInjector
              │  Graph · Resources · Agents    │
              └────────────────────────────────┘
```

The rule that matters most: **the coordinator and the agents talk only through
messages on a `Transport`.** They never share mutable state directly. The
in-process build gets its speed from a zero-copy transport, not from
bypassing the protocol. That keeps the distributed mode honest: if it works
in-process, it works over DDS, apart from latency and message loss, which the
fault injector simulates.

## 3. Repository layout

```
CMakeLists.txt
cmake/                     # toolchain, sanitizer and warning presets
core/
  include/mapc/
    common/                # ids, time, result types, logging, rng
    graph/                 # Graph, GraphBuilder, grid/road loaders
    world/                 # Task, AgentSnapshot, capabilities; WorldState, WorldSnapshot, events
    planning/              # Planner, Dijkstra, AStar, SpaceTimeAStar, ReservationTable
    allocation/            # DistanceOracle, CostModel, TaskAllocator: greedy, cost-greedy, Hungarian
    constraints/           # Constraint types, ConstraintSet
    behavior/              # BT engine: Node, Sequence, Fallback, Decorators, Blackboard
    agent/                 # Agent, AgentController, MobileAgent
    coordinator/           # Coordinator, ResourceManager, HealthMonitor
    messaging/             # Message types, Transport, InProcessTransport
    exec/                  # ThreadPool, PlanningExecutor
    sim/                   # SimulationClock, Simulator, FaultInjector, metrics
  src/...
adapters/
  ros2/                    # colcon package: msgs, Ros2Transport, node executables
scenarios/
  warehouse/  delivery/  drone_search/
apps/
  mapc_sim                 # headless runner: scenario.yaml -> trace.json + metrics
  mapc_bench               # benchmark harness
tests/
  unit/  integration/  property/
tools/
  viewer/                  # optional trace visualizer
docs/
```

`mapc` (multi-agent planning & control) is a working name for the namespace and
library target.

The core library depends only on the C++ standard library, yaml-cpp and
optionally spdlog. **The core never includes a ROS header.** The ROS 2 package
links the core as a normal CMake dependency.

## 4. Core domain model

### 4.1 Identifiers and time

- Strongly typed IDs (`AgentId`, `TaskId`, `NodeId`, `EdgeId`, `ResourceId`) are
  thin wrappers over `uint32_t`, so an agent ID can't be passed where a node ID
  is expected.
- Simulation time is a discrete `Tick` (`int64_t`). Edge traversal costs are
  integer tick durations, which keeps space-time search exact and keeps tests
  free of floating-point flakiness. A scenario maps ticks to seconds for
  reporting.

### 4.2 Graph

- `Graph` is an immutable directed weighted graph in adjacency-array (CSR)
  layout. Edge costs are whole ticks (at least one). Each node has an optional
  position (for heuristics) and a capacity. Tags (zones) are deferred to M8.
  Every built graph has a process-unique `id()`, so overlays and heuristics can
  detect being paired with the wrong graph.
- `GraphBuilder` constructs it and validates the input: unknown nodes, self
  loops, non-positive costs, duplicate edges, non-finite positions and zero
  capacity. The first problem is reported in insertion order, together with the
  index of the offending node or edge, so loaders can point at a line.
- Loaders:
  - `GridMap` builds 4- or 8-connected grids from the MovingAI `.map` format,
    with no corner cutting, as in the benchmarks.
  - `mapc::io::parseGraphYaml` reads explicit node/edge lists for road
    networks. It lives in the separate `mapc_io` library, so `mapc_core` stays
    free of third-party dependencies (yaml-cpp).
- **Dynamic changes do not mutate the `Graph`.** Blocked edges and raised costs
  live in a `GraphOverlay`, and planners search a `GraphView` (graph plus
  optional overlay). The base graph stays shareable across threads without
  locks.
- **Overlays can only raise costs.** Every heuristic is admissible and
  consistent for the base costs, so it stays valid under any overlay.
  Lowering a cost would silently break that, so it is rejected; to make an edge
  cheaper, rebuild the graph.

### 4.3 Tasks

```cpp
struct Task {
    TaskId id;
    NodeId source;
    NodeId destination;
    std::int32_t priority;            // higher = more urgent
    std::optional<Tick> deadline;
    Requirements requirements;        // capabilities now; resource needs in M8
    TaskStatus status;                // Pending, Assigned, InProgress, Done, Failed, Cancelled
};
```

Capabilities are a `CapabilitySet`: a 64-bit mask of small integer indices
that a scenario maps to names (lifting, cold storage, flying). An agent can do
a task when its set contains every capability the task requires. A mask keeps
the check to one instruction and makes snapshots cheap to copy; 64 kinds of
capability is plenty for the scenarios planned.

### 4.4 Agents

The interface from the original plan is kept, with ownership made explicit:

```cpp
class Agent {
public:
    virtual ~Agent() = default;
    virtual AgentId id() const = 0;
    virtual void update(const WorldSnapshot& world, Tick now) = 0;
    virtual void assignTask(const Task& task) = 0;
    virtual AgentSnapshot snapshot() const = 0;
};
```

- Agents are owned as `std::unique_ptr<Agent>` by whoever hosts them: the
  in-process simulator or an agent process.
- Strategies (`TaskAllocator`, `Planner`) **never receive `Agent` objects**.
  They receive `AgentSnapshot` values: id, node, capabilities and current
  task (invalid when idle). The original sketch's `std::vector<Agent>` would not compile, because
  `Agent` is abstract. Passing live polymorphic objects would also let
  allocators reach into agents mid-update. Value snapshots fix both problems.
- `MobileAgent` is the concrete agent. It owns an `AgentController`, which owns
  a behavior tree, a blackboard and a handle to the planning executor.
  Dependencies are injected through the constructor.

### 4.5 World state and snapshots

- `WorldState` holds the overlay, resource states, agent positions and the
  current tick. Only the simulator thread mutates it.
- At each tick boundary the simulator publishes a
  `std::shared_ptr<const WorldSnapshot>`. Planning jobs capture the snapshot by
  value, so a job sees a consistent world even if it finishes several ticks
  later.
- Every snapshot carries a monotonically increasing `version`. A plan records
  the snapshot version it was computed against. Section 6.3 covers what
  happens when it is stale.

## 5. Planning

### 5.1 Interface

```cpp
struct PlanRequest {
    AgentId agent;
    NodeId start;
    NodeId goal;
    Tick start_time;
    SearchLimits limits;  // max_expansions (0 = unlimited)
    // From M5/M8: const ConstraintSet* constraints;
    //             const ReservationTable* reservations;
};

class Planner {
public:
    virtual ~Planner() = default;
    virtual PlanResult plan(const GraphView& view, const PlanRequest& req) = 0;
    virtual std::string_view name() const = 0;
};
```

`PlanResult` holds the outcome and the search statistics:
- The outcome is a `Result<Path, PlanFailure>`. A `Path` is a sequence of
  `(NodeId, Tick)` steps plus its cost. The failure is one of `InvalidRequest`,
  `NoPath` or `LimitReached` (`ConstraintInfeasible` arrives with constraints in
  M5/M8).
- `SearchStats` counts expanded and generated nodes, the largest open list, and
  wall time. Everything except wall time is deterministic.

Search budgets count expansions, not wall-clock time. A timeout would make
results depend on machine load, which conflicts with deterministic simulation.

Planners take a `GraphView` rather than the whole `WorldSnapshot`: the view is
the only part of the world a single-agent search needs. From M4 on, a snapshot
provides the view.

A `Planner` instance is **not** required to be thread-safe. The executor gives
each worker its own instance, created from a `PlannerFactory`. That keeps
scratch buffers lock-free and reusable.

### 5.2 Single-agent planners

All three are one `BestFirstSearch` engine ordered by `f = g + w·h`, without
re-opening closed nodes:

| Planner | Notes |
|---|---|
| Dijkstra | Zero heuristic. An independent Bellman-Ford reference checks it in the property tests. |
| A\* | With a consistent heuristic, returns the same cost as Dijkstra (property-tested on 1,000 random graphs, with and without overlays). |
| Weighted A\* | Path cost is at most `w × optimal`, also asserted on 1,000 random graphs. `w` is an exact fraction, so priorities are computed in integers. |

Heuristics, each bound to one graph:

| Heuristic | Estimate |
|---|---|
| `ZeroHeuristic` | Always zero. |
| `GridHeuristic` | Manhattan distance on 4-connected grids. On 8-connected grids, octile distance, except that a diagonal costing more than two straight moves counts as two straight moves, and diagonals cheaper than straight moves give the diagonal cost × Chebyshev distance. All three cases are consistent lower bounds. |
| `EuclideanHeuristic` | Straight-line distance × the cheapest cost per unit length over all edges, rounded down with a small floating-point safety margin. |

Determinism and performance details:
- Ties are broken by deeper `g`, then smaller node id.
- Per-node scratch arrays are kept between queries and reset in O(1) with a
  generation stamp.

### 5.3 Multi-agent planning

- **Space-time A\*** searches over `(node, tick)` states. A wait action is an
  edge from a node to itself. It respects:
  - vertex reservations: `(node, t)` already taken
  - edge reservations: swap conflicts, where A goes u→v while B goes v→u in the
    same step
  - hard constraints from the `ConstraintSet`
  - a horizon limit, so search terminates in crowded maps
- **ReservationTable**: a hash map keyed on `(node, tick)` and `(edge, tick)`.
  An agent that has reached its goal holds an indefinite reservation on that
  node until it gets a new task (parking).
- **Baseline MAPF strategy: prioritized planning.** Agents plan in priority
  order, and each agent's path is reserved before the next agent plans. This is
  incomplete, so it can fail where a solution exists. The metrics report such
  failures rather than hiding them.
- **Stretch: Conflict-Based Search (CBS)** for small agent groups whose
  prioritized planning fails. It plugs in as another `MultiAgentPlanner`
  strategy.

### 5.4 Constraints

The original sketch had one `Constraint::satisfied(AgentState, WorldState)`
check. A post-hoc check can only reject plans, so the planner would need to
generate and test candidates. Constraints are split into two kinds:

1. **Search constraints** are compiled into data the planner prunes on during
   expansion:
   - `ForbiddenEdge{agent, edge}`
   - `ForbiddenNode{agent, node, [t0, t1)}`
   - `NodeCapacity{node, k}`, enforced through the reservation table
   - `Deadline{task, tick}`, used to prune states where `g + h > deadline`
2. **Validators** implement `bool satisfied(const AgentSnapshot&, const
   WorldSnapshot&)`. They are checked by the coordinator at commit time and by
   the agent at runtime. They cover rules that are hard to encode in search,
   such as "agent B requires resource Y before entering zone Z".

`ConstraintSet` holds both kinds. Scenario YAML declares constraints, and
custom validators register through a factory.

## 6. Coordination

### 6.1 Coordinator responsibilities

- **Task queue**: accepts new, cancelled and reprioritized tasks.
- **Allocation**: runs the configured `TaskAllocator` when idle agents and
  pending tasks are both non-empty, and on events that invalidate assignments.
- **Path commit**: validates proposed paths against the reservation table and
  validators, then commits or rejects them.
- **Resources**: tracks exclusive or counted resources (chargers, docks, zones)
  with lease semantics. A lease has an expiry tick, so a dead agent can't hold a
  resource forever.
- **Conflict detection**: checks committed paths against the latest world
  state. A blocked edge on a committed path triggers `ReplanRequired` for the
  affected agents.
- **Health monitoring**: tracks heartbeats from agent state reports. An agent
  that misses N reports is marked failed. Its reservations and leases are
  released, and its task returns to the queue.

### 6.2 Task allocation

```cpp
class TaskAllocator {
public:
    virtual ~TaskAllocator() = default;
    // Builds the cost matrix, calls solve(), checks and maps the result.
    Assignment allocate(const std::vector<AgentSnapshot>& agents,
                        const std::vector<Task>& tasks,
                        const CostModel& cost) const;
    virtual std::vector<MatrixMatch> solve(const CostMatrix& costs) const = 0;
    virtual std::string_view name() const = 0;
};
```

- `allocate` is the entry point the coordinator uses. It rejects invalid or
  duplicate ids, builds a `CostMatrix` (agents × tasks), and lets the strategy
  `solve` it. Strategies only ever see the matrix, so they are small, stateless
  and testable without a graph. `allocate` also checks every strategy's answer
  (feasible pairs only, each agent and task at most once), so a broken
  strategy fails loudly instead of double-booking an agent.
- Every agent passed in is treated as available. The coordinator passes idle
  agents and pending tasks.
- **Capability mismatches are infeasible** and are never assigned.
  `CostMatrix::build` marks them itself, without asking the cost model, so no
  cost model can get this wrong.
- `CostModel` estimates the cost of agent *a* doing task *t*. The default
  `TravelCostModel` is the travel time from the agent to the source, plus
  source to destination, minus an optional priority bonus (ticks per priority
  level, so urgent tasks win when there are more tasks than agents). A pair is
  infeasible if either leg is unreachable. Deadline penalties arrive with
  deadline-aware allocation in M8.
- Travel times come from a `DistanceOracle`: exact shortest-path distances,
  cached per source node. The first query from a node runs one full Dijkstra
  search and keeps the whole row (8 bytes per graph node), so allocators never
  run a planner per pair. Agents and task sources repeat between rounds, so
  the cache pays off over a run. The oracle honours the overlay and drops its
  cache when the overlay's version changes. Exact distances were chosen over
  landmark bounds because allocation quality is compared against an optimum,
  and lower bounds would blur that comparison.
- Costs are `TaskCost` values: whole ticks within ±2^40, or infeasible. The
  limit keeps every sum an allocator forms inside 64 bits; the cost model
  throws instead of wrapping if a cost would exceed it.
- Strategies:
  - **Greedy nearest**: tasks, in the order given, each take the cheapest
    free agent. With `TravelCostModel` the only agent-dependent part of a
    task's cost is the approach distance, so this is "nearest free agent
    first". O(A·T). The coordinator orders the tasks, for example by
    priority and arrival.
  - **Cost-based greedy**: picks the globally cheapest (agent, task) pair
    repeatedly. O(A·T·log(A·T)).
  - **Hungarian (Kuhn–Munkres)**: optimal for one-shot assignment. It assigns
    as many pairs as possible and, among those, minimises the total cost.
    Rectangular matrices are solved directly in O(n²·m) for n = min(A, T),
    m = max(A, T), rather than padded to a square, so a few agents never pay
    for a long task list squared. Infeasible pairs get a cost M larger than
    the cost range of any whole assignment, and one extra "unassigned" column
    keeps the dual potentials bounded by M, so the arithmetic is exact in 64
    bits. A property test checks it against brute force on random instances
    up to 7 × 7.
  - **Auction (stretch)**: Bertsekas-style. Its main use is in distributed
    mode, where agents bid over the transport.
- Every strategy is deterministic: the same input gives the same assignment.
  The greedy strategies break ties by lower agent index, then lower task
  index.

### 6.3 Planning concurrency and plan commit

The obvious approach, where every agent plans in parallel, conflicts with
prioritized planning, which is sequential by definition. The design resolves
this with **optimistic concurrency control**:

1. Agents request plans independently. Jobs run in parallel on the executor
   against the current snapshot and a *copy* of the reservation table at a
   known version.
2. A finished plan is sent to the coordinator as a `PathProposal` that carries
   the reservation-table version it was planned against.
3. The coordinator commits proposals one at a time. A proposal from an older
   version is rechecked against the reservations committed since. If there is
   no conflict, it is committed. If there is a conflict, it is rejected and the
   agent replans against the newer table.
4. A retry cap with escalating priority prevents starvation.

This gives real parallel speedup when agents are spread out, and it degrades
to sequential behavior under heavy contention. The benchmark should measure
exactly that trade-off. The same protocol works unchanged across processes.

### 6.4 Messages

| Message | Direction | Purpose |
|---|---|---|
| `TaskAssigned` | C → A | task plus assignment epoch |
| `TaskCancelled` | C → A | revoke assignment |
| `PathProposal` | A → C | path plus reservation version |
| `PathCommitted` / `PathRejected` | C → A | commit result and conflict reason |
| `ReplanRequired` | C → A | world change affects committed path |
| `AgentStateReport` | A → C | position, BT status, task progress; doubles as heartbeat |
| `TaskStatusUpdate` | A → C | InProgress / Done / Failed |
| `WorldEvent` | Sim → all | edge blocked or unblocked, cost change, resource down |

Every message carries a sequence number and an assignment epoch, so stale or
duplicated messages are ignored. This matters once delays and reordering are
injected.

`Transport` exposes `publish(Topic, Message)` and `subscribe(Topic, Handler)`.
`InProcessTransport` delivers messages at tick boundaries in deterministic
order. `Ros2Transport` maps topics to ROS 2 topics with custom `.msg` types.

## 7. Behavior trees

The project uses a small BT engine (about 900 lines including headers),
written for this framework, rather than BehaviorTree.CPP:

- The engine is a core part of the project's value and needs to be fully
  understood and defensible.
- No external dependency in core.
- Ticking can be made deterministic and tied to the simulation clock.

An adapter for BehaviorTree.CPP can come later if XML-authored trees are
needed.

The engine lives in `mapc/behavior/`, namespace `mapc::bt` (its short names,
such as `Status` and `Sequence`, would otherwise be ambiguous in `mapc`).

**Ticking.** `Tree::tick(now)` ticks the root with the simulation time, which
must never go backwards; no node reads a wall clock. Statuses are `Success`,
`Failure` and `Running`. A node that returns `Running` is ticked again on a
later tick to continue, or halted to abandon its work. `halt()` reaches only
running nodes and halts everything running below them. A node that is not
running holds no in-progress state, so its next tick starts it afresh. If a
node throws, it and its ancestors are halted as the exception unwinds, so the
tree is never left with a node that believes it is running. Destroying a tree
halts it.

**Node types**:

| Node | Semantics |
|---|---|
| `Sequence` | children in order until one fails; resumes at a `Running` child (memory) |
| `Fallback` (selector) | children in order until one succeeds; resumes at a `Running` child |
| `ReactiveSequence` | restarts at the first child every tick; a failing or `Running` child halts any later child still running |
| `ReactiveFallback` | restarts at the first child every tick; a succeeding or `Running` child halts any later child still running, so a higher-priority branch preempts a lower one |
| `Parallel(k-of-n)` | ticks unfinished children; succeeds at k successes, fails once k is out of reach, then halts the rest |
| `Inverter` | swaps `Success` and `Failure` |
| `Retry(n)` | up to n attempts; a failed attempt returns `Running` and the next attempt starts on the next tick (within one tick the world is unchanged, so an immediate retry would fail the same way) |
| `Timeout(ticks)` | the child is ticked only while less than `ticks` have passed since the run started; at the limit it is halted and `Timeout` fails |
| `RateLimit(ticks)` | starts the child at most once per period; in between it returns the last completed result without ticking the child; a running child is ticked every tick |
| `Condition`, `Action` | leaves from lambdas; an `Action` may have a halt callback |
| `StatefulAction` | base class for leaves with separate start, continue and halt hooks |

A shared, typed `Blackboard` stores values in `std::any`, and every access goes
through a `BlackboardKey<T>`, so a value is always read back as the type it was
stored as (a mismatch throws). Trees are composed with a small C++ DSL
(`sequence(...)`, `retry(3, ...)` and so on) that mirrors the tree's shape, and
`dumpTree` prints a tree as indented ASCII with each node's last state, for
debugging and traces.

**Default agent tree** (domain-agnostic):

```
ReactiveFallback
├── Sequence [Recover]
│   ├── Condition: FaultDetected?
│   └── Action: Recover (stop, release, report)
├── Sequence [ExecuteTask]
│   ├── Condition: HasTask?
│   ├── Condition: TaskStillValid?
│   ├── Fallback
│   │   ├── Condition: HasCommittedPath?
│   │   └── Retry(3) → Action: RequestPlan (async, Running until result)
│   ├── ReactiveSequence
│   │   ├── Condition: PathStillValid?   (fails on ReplanRequired → clears path)
│   │   └── Action: FollowPath
│   └── Action: CompleteTask
└── Action: Idle / AcquireTask
```

`RequestPlan` submits work to the `PlanningExecutor` and returns `Running`. A
later tick sees the future resolve and returns `Success` or `Failure`, so the
BT never blocks the simulation thread. Scenario-specific actions such as Pickup
or Scan are leaf nodes registered by the scenario.

M3 exercises this shape with a toy agent (`apps/mapc_bt_demo`) in a fake grid
world, with synchronous planning. It adds one leaf to the tree above: an
`AbandonTask` action after `Retry(3) → RequestPlan` in the path fallback, so a
task that cannot be planned is given up and the agent takes the next one.

## 8. Execution and threading model

| Thread | Owns | Notes |
|---|---|---|
| Simulation thread | `WorldState`, clock, agent `update()` calls, transport delivery | single writer, so no locks on world state |
| Worker pool (N) | planner instances, per-thread scratch buffers | reads immutable snapshots only |
| Metrics thread (optional) | aggregation, periodic flush | fed through a lock-free or mutex-guarded queue |
| Transport thread (ROS 2 only) | rclcpp executor | hands messages to the sim thread through a queue |

**Thread pool**: hand-written `ThreadPool` using `std::mutex`,
`std::condition_variable` and a task deque, with
`submit(F) -> std::future<R>`, cooperative cancellation through an
`std::atomic<bool>` stop token in each job, and graceful shutdown that joins
in the destructor (RAII).

**Synchronization primitives used deliberately**:

- `std::shared_ptr<const T>` for snapshot publication. Readers never lock.
- `std::mutex` plus `std::condition_variable` for the job queue.
- `std::atomic` for cancellation flags and metrics counters.
- `std::shared_mutex` only where a read-mostly structure really is shared, such
  as the `CostModel` distance cache.

**Determinism with a thread pool**: wall-clock completion order varies between
runs. Results are therefore collected at tick boundaries and applied in a
canonical order, sorted by `(agent priority, AgentId)`. A setting can make a
job finish in a fixed number of simulated ticks, so trace output does not
depend on thread scheduling. A test checks this: the same seed with 1 worker
and with 8 workers must produce byte-identical traces.

## 9. Simulation, fault injection and metrics

### 9.1 Simulator

`Simulator::step()`:

1. apply scheduled world events and injected faults
2. publish the new snapshot
3. deliver queued messages in deterministic order
4. coordinator step
5. agent `update()` calls (BT ticks)
6. collect completed planning jobs
7. advance agents along committed paths
8. record trace and metrics

Simulated agents move exactly as their committed paths say, unless a fault
says otherwise, so plan execution is exact.

### 9.2 Fault injector

Faults come from a scripted timeline in scenario YAML, from seeded random
injection, or from both:

| Fault | Effect |
|---|---|
| `block_edge` / `unblock_edge` | overlay change, then `ReplanRequired` for affected paths |
| `change_cost` | overlay change; replan only if it beats a threshold |
| `resource_down` | leases revoked, dependent tasks re-queued |
| `agent_fail` | agent stops reporting; health monitor recovers its task |
| `agent_stall(n)` | agent holds position for n ticks; neighbors' reservations conflict |
| `message_delay(topic, d)` / `message_drop(p)` | applied inside `InProcessTransport` |
| `inject_task(priority)` / `cancel_task` | dynamic workload |

### 9.3 Metrics

Each run reports:

- Throughput (tasks per 1,000 ticks), makespan and sum of costs
- Task latency (assignment to completion) at p50, p95 and p99
- Deadline misses
- Replans, broken down by cause, and proposal rejections (OCC retries)
- Prioritized-planning failures
- Planner wall time per call at p50, p95 and p99, plus nodes expanded
- Speedup curve: wall time for N agents against worker count 1, 2, 4 and 8

Output is a `trace.json` (per-tick agent positions and events) and a
`metrics.json`. The viewer and the benchmark scripts consume both.

## 10. Scenarios

| Scenario | Graph | Depth | Demonstrates |
|---|---|---|---|
| Warehouse (flagship) | grid with shelves and aisles, MovingAI-compatible | full | pickup-and-delivery tasks, docks as capacity-1 resources, chargers as leased resources, dense congestion and OCC rejections, blocked aisles |
| Delivery fleet | road network graph (node/edge list with real-ish coordinates) | small | non-grid graph, Euclidean heuristic, cost changes (traffic), deadlines |
| Drone search | open grid with no-fly zones | small | coverage-style tasks generated at runtime, `ForbiddenNode` constraints, `report_target` leaf action |

Each scenario consists of a YAML file plus at most one source file that
registers its custom BT leaves. If a scenario needs core changes, the
abstraction is wrong. That rule is the test of the "domain-agnostic" claim.

## 11. Distributed mode (ROS 2)

- Target ROS 2 **Jazzy** (the LTS release for Ubuntu 24.04), built with colcon
  under `adapters/ros2/`.
- Message definitions mirror §6.4 in a `mapc_msgs` package.
- Executables:
  - `mapc_coordinator_node`, which hosts the `Coordinator`
  - `mapc_agent_node --id N`, which hosts one `MobileAgent` and its own
    executor
  - `mapc_world_node`, which hosts the simulator and the source of truth for
    the world
- A launch file starts the coordinator, the world node and N agent nodes.
- QoS settings: state reports use best-effort with keep-last 1. Tasks and path
  commits use reliable delivery with transient-local durability, so late
  joiners receive current assignments.
- Clock: the world node publishes `/clock`, and all nodes use simulated time.
  This keeps distributed runs reproducible enough for integration tests, but
  they are **not** bitwise deterministic, and the documentation should say so.
- Success criterion: the same warehouse scenario runs in-process and
  distributed with the same completion count, and the distributed run survives
  killing an agent process.

## 12. Build, quality and CI

- CMake ≥ 3.20 with presets `debug`, `release`, `asan-ubsan` and `tsan`. TSan
  must run in its own build because it is incompatible with ASan.
- Warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` in CI.
- `clang-tidy` (modernize, bugprone, concurrency, performance) and
  `clang-format`.
- Dependencies come through CMake `FetchContent` with pinned versions:
  GoogleTest, yaml-cpp and Google Benchmark.
- GitHub Actions jobs:
  - Build and test on GCC and Clang
  - The `asan-ubsan` and `tsan` presets
  - clang-tidy
  - Coverage report
  - A ROS 2 job using the `ros:jazzy` container
- Tests:
  - **Unit tests** for each module.
  - **Property tests**: A\* cost equals Dijkstra cost on random graphs;
    Hungarian matches brute force on n ≤ 7; a committed path set never
    contains a vertex or swap conflict, checked by an independent validator.
  - **Integration tests**: full scenario runs with fixed seeds and expected
    metric ranges.
  - **Determinism test**: identical traces across worker counts.
  - **Fault tests**: each fault type in §9.2 has a scenario that triggers it
    and asserts recovery.

## 13. Key risks and decisions

| Risk / decision | Mitigation / choice |
|---|---|
| Scope creep: eight modules plus ROS 2 is a lot | Milestones in ROADMAP.md each end in a demoable state. ROS 2 and stretch items come last. |
| Prioritized planning deadlocks in narrow corridors | Horizon-limited planning, waiting at the goal, plus retry with randomized priorities. Failures are reported in metrics. CBS is the stretch fix. |
| Parallel planning speedup is disappointing under contention | Expected, and the OCC rejection metrics explain it. Benchmarks should include sparse and dense maps. |
| Non-determinism leaks in through threads | Results are applied at tick boundaries in canonical order, guarded by the determinism test. |
| ROS 2 ends up spread through the codebase | Core never includes ROS headers. The CI core build doesn't install ROS. |
| C++17 vs C++20 (`std::span`, `std::jthread`, concepts) | Stay on C++17 as specified. Use `gsl::span`-style views or `const std::vector&`, and add a small `StopToken`. A C++20 migration can be a later, separate milestone. |
| BT engine: custom vs BehaviorTree.CPP | Custom engine in core (see §7), with an optional adapter later. |

## 14. Open questions

1. Should agents plan locally (in-agent, as in §6.3) or should the coordinator
   plan centrally? The design assumes local planning with central commit,
   because it maps cleanly to distributed mode. Central planning would be
   simpler but makes agents thin.
2. YAML or JSON for scenarios? The draft uses YAML (yaml-cpp) because it reads
   better and is ROS-idiomatic.
3. Viewer: terminal ASCII renderer, a small web page reading `trace.json`, or
   both? An ASCII renderer comes first because it costs almost nothing; the web
   viewer is optional.
4. Should the delivery and drone scenarios share the warehouse BT, extended
   with leaves, or define their own trees? Sharing the tree is the stronger
   demonstration of genericity.
