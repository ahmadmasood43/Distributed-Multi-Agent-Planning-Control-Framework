#pragma once

// A toy agent ticking a behavior tree against a fake world. The tree has the
// shape of the default agent tree in docs/DESIGN.md §7 (recover, execute a
// task, otherwise acquire one); the world is a small grid with a task queue,
// cells that can be blocked and a fault flag, all changed by a script.

#include "mapc/behavior/tree.hpp"
#include "mapc/common/strong_id.hpp"
#include "mapc/common/time.hpp"
#include "mapc/graph/graph_overlay.hpp"
#include "mapc/graph/grid_map.hpp"
#include "mapc/planning/best_first_search.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace mapc::apps {

/// Go to `goal`.
struct ToyTask {
    TaskId id;
    GridCell goal;
};

/// One agent on a grid map, a queue of tasks, blocked cells and a fault flag.
class ToyWorld {
public:
    /// Throws std::invalid_argument if `start` is not a passable cell.
    ToyWorld(GridMap map, GridCell start);
    ToyWorld(const ToyWorld&) = delete;
    ToyWorld& operator=(const ToyWorld&) = delete;
    ToyWorld(ToyWorld&&) = delete;
    ToyWorld& operator=(ToyWorld&&) = delete;
    ~ToyWorld() = default;

    [[nodiscard]] const GridMap& map() const noexcept { return map_; }
    /// The map with blocked cells applied.
    [[nodiscard]] GraphView view() const { return GraphView(map_.graph(), overlay_); }

    [[nodiscard]] NodeId agentNode() const noexcept { return agent_; }
    [[nodiscard]] GridCell agentCell() const { return map_.cellOf(agent_); }

    /// Moves the agent along `edge` if the edge starts at the agent and is
    /// not blocked. Returns whether it moved.
    bool move(EdgeId edge);

    /// A blocked cell cannot be entered. Throws std::invalid_argument for a
    /// cell that is not passable on the map, or (when blocking) for the
    /// agent's own cell.
    void setBlocked(GridCell cell, bool blocked);
    [[nodiscard]] bool blocked(GridCell cell) const;

    [[nodiscard]] bool faulted() const noexcept { return faulted_; }
    void setFaulted(bool faulted) noexcept { faulted_ = faulted; }

    /// Throws std::invalid_argument if the goal is not a passable cell.
    void addTask(const ToyTask& task);
    [[nodiscard]] std::optional<ToyTask> takeTask();
    [[nodiscard]] std::size_t queuedTasks() const noexcept { return queue_.size(); }

private:
    [[nodiscard]] NodeId nodeOf(GridCell cell) const;

    GridMap map_;
    GraphOverlay overlay_;
    NodeId agent_;
    std::vector<bool> blocked_;  // by node
    bool faulted_ = false;
    std::deque<ToyTask> queue_;
};

struct ToyAgentStats {
    std::vector<TaskId> completed;
    std::vector<TaskId> abandoned;  ///< no path after every planning attempt
    std::vector<TaskId> cancelled;  ///< goal blocked before the task started
    std::size_t plans = 0;          ///< successful planning requests
    std::size_t plan_failures = 0;
    std::size_t path_invalidations = 0;  ///< committed path blocked while following it
    std::size_t recoveries = 0;          ///< faults handled
    std::size_t follow_halts = 0;        ///< times FollowPath was halted mid-path
    std::size_t moves = 0;
};

/// An agent controlled by a behavior tree:
///
///     ReactiveFallback "agent"
///     +-- Sequence "recover"
///     |   +-- Condition "fault detected?"
///     |   `-- Action "recover"               stop, drop the path; Running while faulted
///     +-- Sequence "execute task"
///     |   +-- Condition "has task?"
///     |   +-- Condition "task still valid?"  goal not blocked, else cancel it
///     |   +-- Fallback "ensure path"
///     |   |   +-- Condition "has path?"
///     |   |   +-- Retry(3)
///     |   |   |   `-- Action "request plan"  A* on the world with blocked cells
///     |   |   `-- Action "abandon task"      fails, so the next task is taken
///     |   +-- ReactiveSequence "follow"
///     |   |   +-- Condition "path still valid?"
///     |   |   `-- Action "follow path"       one edge per tick
///     |   `-- Action "complete task"
///     `-- Action "acquire task"              Running while there is nothing to do
///
/// Planning is synchronous here; the asynchronous version arrives with the
/// planning executor.
class ToyAgent {
public:
    using LogSink = std::function<void(const std::string&)>;

    /// `log` receives one line per notable action.
    ToyAgent(ToyWorld& world, LogSink log);

    bt::Status tick(Tick now);

    [[nodiscard]] const bt::Tree& tree() const noexcept { return tree_; }
    [[nodiscard]] const ToyAgentStats& stats() const noexcept { return stats_; }
    /// The task in progress, if any.
    [[nodiscard]] std::optional<ToyTask> currentTask() const;

private:
    [[nodiscard]] bt::NodePtr buildTree();
    void log(const std::string& line) const;

    ToyWorld* world_;
    LogSink log_;
    AStarPlanner planner_;
    ToyAgentStats stats_;
    bt::Tree tree_;
};

/// A scripted change to the world at a given tick, applied before the agent
/// is ticked.
struct ToyEvent {
    Tick at;
    std::string description;
    std::function<void(ToyWorld&)> apply;
    /// Print the tree after the agent's tick at this time.
    bool dump_tree = false;
};

struct ToyScenario {
    std::vector<std::string> rows;
    GridCell start;
    std::vector<ToyEvent> events;  ///< in time order
    Tick max_time{200};
};

/// The demo scenario: four tasks; an aisle that is blocked and later reopened
/// (replan), a fault (preemption and recovery), a goal that is sealed off
/// (abandoned after three failed plans) and a goal that is blocked (task
/// cancelled).
[[nodiscard]] ToyScenario demoScenario();

struct ToyRunResult {
    ToyAgentStats stats;
    std::vector<GridCell> positions;  ///< agent cell after each tick, from tick 0
    Tick end;                         ///< last tick run
    bool finished = false;            ///< all tasks resolved before max_time
};

/// Runs `scenario` until every task is resolved and no events remain, or
/// until max_time. Writes a log to `out`, plus a tree dump after the first
/// tick, after ticks whose events ask for one, and after every tick if
/// `dump_every_tick`.
[[nodiscard]] ToyRunResult runToyScenario(const ToyScenario& scenario, std::ostream& out,
                                          bool dump_every_tick = false);

/// The mapc_bt_demo command line (without the program name). Returns the exit
/// code: 0 when the scenario finished, 1 if it hit max_time, 2 for bad
/// arguments.
///
///   mapc_bt_demo [--dump-every-tick]
int runBtDemoCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace mapc::apps
