#include "toy_agent.hpp"

#include "mapc/behavior/builder.hpp"
#include "mapc/planning/heuristics.hpp"
#include "mapc/planning/path.hpp"

#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mapc::apps {

namespace {

using bt::BlackboardKey;
using bt::Status;
using bt::TickContext;

constexpr BlackboardKey<ToyTask> kTask{"task"};
constexpr BlackboardKey<Path> kPath{"path"};
constexpr BlackboardKey<std::size_t> kStep{"step"};  // index of the agent's step in kPath

constexpr std::uint32_t kPlanAttempts = 3;

std::string cellText(GridCell cell) {
    return "(" + std::to_string(cell.x) + "," + std::to_string(cell.y) + ")";
}

std::string taskText(const ToyTask& task) {
    return toString(task.id) + " -> " + cellText(task.goal);
}

/// "recover": stop and drop the path when a fault appears; Running until the
/// fault clears.
class Recover final : public bt::StatefulAction {
public:
    Recover(const ToyWorld& world, ToyAgentStats& stats, ToyAgent::LogSink log)
        : StatefulAction("recover"), world_(&world), stats_(&stats), log_(std::move(log)) {}

protected:
    Status onStart(TickContext& context) override {
        ++stats_->recoveries;
        const bool had_path = context.blackboard().erase(kPath.name());
        log_(std::string("fault: stopped") + (had_path ? ", path released" : ""));
        return world_->faulted() ? Status::Running : Status::Success;
    }
    Status onRunning(TickContext& /*context*/) override {
        if (world_->faulted()) {
            return Status::Running;
        }
        log_("fault cleared: resuming");
        return Status::Success;
    }

private:
    const ToyWorld* world_;
    ToyAgentStats* stats_;
    ToyAgent::LogSink log_;
};

}  // namespace

// --- ToyWorld -------------------------------------------------------------------

ToyWorld::ToyWorld(GridMap map, GridCell start)
    : map_(std::move(map)), overlay_(map_.graph()), blocked_(map_.graph().nodeCount(), false) {
    const std::optional<NodeId> node = map_.nodeAt(start.x, start.y);
    if (!node) {
        throw std::invalid_argument("ToyWorld: start " + cellText(start) + " is not passable");
    }
    agent_ = *node;
}

NodeId ToyWorld::nodeOf(GridCell cell) const {
    const std::optional<NodeId> node = map_.nodeAt(cell.x, cell.y);
    if (!node) {
        throw std::invalid_argument("ToyWorld: " + cellText(cell) + " is not a passable cell");
    }
    return *node;
}

bool ToyWorld::move(EdgeId edge) {
    const Graph& graph = map_.graph();
    if (!graph.contains(edge) || graph.source(edge) != agent_ || overlay_.isBlocked(edge)) {
        return false;
    }
    agent_ = graph.target(edge);
    return true;
}

void ToyWorld::setBlocked(GridCell cell, bool blocked) {
    const NodeId node = nodeOf(cell);
    if (blocked && node == agent_) {
        throw std::invalid_argument("ToyWorld: cannot block the agent's cell " + cellText(cell));
    }
    blocked_[node.value()] = blocked;
    // Block every edge into the cell; the grid's edges come in pairs.
    const Graph& graph = map_.graph();
    for (const EdgeId out : graph.outEdges(node)) {
        if (const std::optional<EdgeId> in = graph.findEdge(graph.target(out), node)) {
            if (blocked) {
                (void)overlay_.block(*in);
            } else {
                (void)overlay_.unblock(*in);
            }
        }
    }
}

bool ToyWorld::blocked(GridCell cell) const {
    return blocked_[nodeOf(cell).value()];
}

void ToyWorld::addTask(const ToyTask& task) {
    (void)nodeOf(task.goal);
    queue_.push_back(task);
}

std::optional<ToyTask> ToyWorld::takeTask() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    ToyTask task = queue_.front();
    queue_.pop_front();
    return task;
}

// --- ToyAgent -------------------------------------------------------------------

ToyAgent::ToyAgent(ToyWorld& world, LogSink log)
    : world_(&world),
      log_(std::move(log)),
      planner_(std::make_shared<GridHeuristic>(world.map())),
      tree_(buildTree()) {}

Status ToyAgent::tick(Tick now) {
    return tree_.tick(now);
}

std::optional<ToyTask> ToyAgent::currentTask() const {
    const ToyTask* task = tree_.blackboard().find(kTask);
    return task == nullptr ? std::nullopt : std::optional<ToyTask>(*task);
}

void ToyAgent::log(const std::string& line) const {
    if (log_) {
        log_(line);
    }
}

bt::NodePtr ToyAgent::buildTree() {
    using namespace bt;  // NOLINT(google-build-using-namespace): the tree DSL

    auto fault_detected = [this](TickContext&) { return world_->faulted(); };

    auto has_task = [](TickContext& ctx) { return ctx.blackboard().contains(kTask.name()); };

    auto task_still_valid = [this](TickContext& ctx) {
        const ToyTask task = ctx.blackboard().get(kTask);
        if (!world_->blocked(task.goal)) {
            return true;
        }
        log("cancel " + taskText(task) + ": goal is blocked");
        stats_.cancelled.push_back(task.id);
        ctx.blackboard().erase(kTask.name());
        ctx.blackboard().erase(kPath.name());
        return false;
    };

    auto has_path = [](TickContext& ctx) { return ctx.blackboard().contains(kPath.name()); };

    auto request_plan = [this](TickContext& ctx) {
        const ToyTask task = ctx.blackboard().get(kTask);
        PlanRequest request;
        request.start = world_->agentNode();
        request.goal = *world_->map().nodeAt(task.goal.x, task.goal.y);
        request.start_time = ctx.now();
        PlanResult result = planner_.plan(world_->view(), request);
        if (!result.found()) {
            ++stats_.plan_failures;
            log("plan " + taskText(task) + ": " + std::string(toString(result.outcome.error())));
            return Status::Failure;
        }
        ++stats_.plans;
        log("plan " + taskText(task) + ": " + std::to_string(result.outcome.value().cost.count()) +
            " moves");
        ctx.blackboard().set(kPath, std::move(result.outcome).value());
        ctx.blackboard().set(kStep, std::size_t{0});
        return Status::Success;
    };

    auto abandon_task = [this](TickContext& ctx) {
        const ToyTask task = ctx.blackboard().get(kTask);
        log("abandon " + taskText(task) + ": no path after " + std::to_string(kPlanAttempts) +
            " attempts");
        stats_.abandoned.push_back(task.id);
        ctx.blackboard().erase(kTask.name());
        return Status::Failure;
    };

    auto path_still_valid = [this](TickContext& ctx) {
        const Path& path = ctx.blackboard().get(kPath);
        const std::size_t step = ctx.blackboard().get(kStep);
        const GraphView view = world_->view();
        for (std::size_t i = step; i + 1 < path.steps.size(); ++i) {
            const std::optional<EdgeId> edge =
                view.graph().findEdge(path.steps[i].node, path.steps[i + 1].node);
            if (!edge || !view.traversable(*edge)) {
                ++stats_.path_invalidations;
                log("path blocked at " + cellText(world_->map().cellOf(path.steps[i + 1].node)) +
                    ": replanning");
                ctx.blackboard().erase(kPath.name());
                return false;
            }
        }
        return true;
    };

    auto follow_path = [this](TickContext& ctx) {
        const Path& path = ctx.blackboard().get(kPath);
        std::size_t& step = ctx.blackboard().get(kStep);
        if (step + 1 >= path.steps.size()) {
            return Status::Success;
        }
        const std::optional<EdgeId> edge =
            world_->map().graph().findEdge(path.steps[step].node, path.steps[step + 1].node);
        if (!edge || !world_->move(*edge)) {
            return Status::Failure;
        }
        ++stats_.moves;
        ++step;
        return step + 1 >= path.steps.size() ? Status::Success : Status::Running;
    };
    auto stop_following = [this] {
        ++stats_.follow_halts;
        log("follow path halted at " + cellText(world_->agentCell()));
    };

    auto complete_task = [this](TickContext& ctx) {
        const ToyTask task = ctx.blackboard().get(kTask);
        log("done " + taskText(task));
        stats_.completed.push_back(task.id);
        ctx.blackboard().erase(kTask.name());
        ctx.blackboard().erase(kPath.name());
        return Status::Success;
    };

    auto acquire_task = [this](TickContext& ctx) {
        if (ctx.blackboard().contains(kTask.name())) {
            return Status::Running;  // the task branch failed this tick; retry next tick
        }
        const std::optional<ToyTask> task = world_->takeTask();
        if (!task) {
            return Status::Running;  // idle
        }
        log("acquire " + taskText(*task));
        ctx.blackboard().set(kTask, *task);
        return Status::Success;
    };

    auto log_line = [this](const std::string& line) { log(line); };

    return reactiveFallback(
        "agent",
        sequence("recover", condition("fault detected?", fault_detected),
                 node<Recover>(*world_, stats_, log_line)),
        sequence("execute task", condition("has task?", has_task),
                 condition("task still valid?", task_still_valid),
                 fallback("ensure path", condition("has path?", has_path),
                          retry(kPlanAttempts, action("request plan", request_plan)),
                          action("abandon task", abandon_task)),
                 reactiveSequence("follow", condition("path still valid?", path_still_valid),
                                  action("follow path", follow_path, stop_following)),
                 action("complete task", complete_task)),
        action("acquire task", acquire_task));
}

// --- Scenario -------------------------------------------------------------------

ToyScenario demoScenario() {
    ToyScenario scenario;
    scenario.rows = {
        "##########",  //
        "#........#",  // y = 1
        "#.##.###.#",  // y = 2
        "#........#",  // y = 3
        "#.######.#",  // y = 4
        "#........#",  // y = 5
        "##########",
    };
    scenario.start = {1, 1};
    auto add = [](TaskId::rep_type id, GridCell goal) {
        return [id, goal](ToyWorld& world) { world.addTask({TaskId{id}, goal}); };
    };
    auto block = [](GridCell cell, bool blocked) {
        return [cell, blocked](ToyWorld& world) { world.setBlocked(cell, blocked); };
    };
    scenario.events = {
        {Tick{0}, "tasks 1-4 arrive",
         [add](ToyWorld& world) {
             add(1, {8, 5})(world);
             add(2, {4, 2})(world);
             add(3, {8, 1})(world);
             add(4, {1, 5})(world);
         }},
        {Tick{3}, "aisle (8,2) blocked", block({8, 2}, true), true},
        {Tick{9}, "fault", [](ToyWorld& world) { world.setFaulted(true); }, true},
        {Tick{12}, "fault cleared", [](ToyWorld& world) { world.setFaulted(false); }},
        {Tick{14}, "alcove (4,2) sealed off",
         [block](ToyWorld& world) {
             block({4, 1}, true)(world);
             block({4, 3}, true)(world);
         }},
        {Tick{15}, "goal (1,5) blocked", block({1, 5}, true)},
        {Tick{19}, "aisle (8,2) reopened", block({8, 2}, false)},
    };
    return scenario;
}

ToyRunResult runToyScenario(const ToyScenario& scenario, std::ostream& out, bool dump_every_tick) {
    Result<GridMap, LoadError> map = GridMap::fromRows(scenario.rows);
    if (!map) {
        throw std::invalid_argument("toy scenario: bad map: " + map.error().describe());
    }
    ToyWorld world(std::move(map).value(), scenario.start);

    Tick now = Tick::zero();
    ToyAgent agent(world,
                   [&](const std::string& line) { out << "  " << now << "  " << line << '\n'; });

    ToyRunResult result;
    std::size_t next_event = 0;
    for (; now <= scenario.max_time; ++now) {
        bool dump_tree = false;
        while (next_event < scenario.events.size() && scenario.events[next_event].at <= now) {
            const ToyEvent& event = scenario.events[next_event++];
            out << "  " << now << "  * " << event.description << '\n';
            event.apply(world);
            dump_tree = dump_tree || event.dump_tree;
        }
        const bt::Status status = agent.tick(now);
        result.positions.push_back(world.agentCell());
        result.end = now;
        if (dump_tree || dump_every_tick || now == Tick::zero()) {
            out << "\n  tree after " << now << " (" << toString(status) << "), agent at "
                << cellText(world.agentCell()) << ":\n";
            std::istringstream dump(agent.tree().dump());
            for (std::string line; std::getline(dump, line);) {
                out << "    " << line << '\n';
            }
            out << '\n';
        }
        const bool idle = !agent.currentTask() && world.queuedTasks() == 0;
        if (idle && next_event == scenario.events.size() && !world.faulted()) {
            result.finished = true;
            break;
        }
    }
    result.stats = agent.stats();

    const ToyAgentStats& s = result.stats;
    out << "\nsummary: " << (result.finished ? "finished" : "stopped") << " at " << result.end
        << "; completed " << s.completed.size() << ", abandoned " << s.abandoned.size()
        << ", cancelled " << s.cancelled.size() << "; " << s.moves << " moves, " << s.plans
        << " plans, " << s.plan_failures << " failed plans, " << s.path_invalidations
        << " path invalidations, " << s.recoveries << " recoveries\n";
    return result;
}

int runBtDemoCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    bool dump_every_tick = false;
    for (const std::string& arg : args) {
        if (arg == "--dump-every-tick") {
            dump_every_tick = true;
        } else {
            err << "usage: mapc_bt_demo [--dump-every-tick]\n";
            return 2;
        }
    }
    const ToyRunResult result = runToyScenario(demoScenario(), out, dump_every_tick);
    return result.finished ? 0 : 1;
}

}  // namespace mapc::apps
