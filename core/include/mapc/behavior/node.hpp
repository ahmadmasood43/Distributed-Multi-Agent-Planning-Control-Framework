#pragma once

#include "mapc/behavior/blackboard.hpp"
#include "mapc/common/time.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace mapc::bt {

/// What a node reports when it is ticked.
enum class Status : std::uint8_t {
    Success,
    Failure,
    Running,  ///< not finished yet; tick again later
};

/// What a node did most recently, for inspection and tree dumps.
enum class NodeState : std::uint8_t {
    Idle,     ///< never ticked
    Running,  ///< its last tick returned Running and it has not been halted since
    Success,  ///< its last tick returned Success
    Failure,  ///< its last tick returned Failure
    Halted,   ///< it was Running and then halted
};

[[nodiscard]] std::string_view toString(Status status) noexcept;
[[nodiscard]] std::string_view toString(NodeState state) noexcept;

/// What a node sees when it is ticked: the simulation time and the tree's
/// blackboard. Time comes from the caller, never from a wall clock, so a tree
/// behaves the same on every run.
class TickContext {
public:
    TickContext(Tick now, Blackboard& blackboard) noexcept : now_(now), blackboard_(&blackboard) {}

    [[nodiscard]] Tick now() const noexcept { return now_; }
    [[nodiscard]] Blackboard& blackboard() const noexcept { return *blackboard_; }

private:
    Tick now_;
    Blackboard* blackboard_;
};

/// A behavior tree node.
///
/// `tick()` runs the node once and returns its status. A node that returns
/// Running is in the middle of something: it is ticked again on a later tick
/// to continue, or halted to abandon it. `halt()` stops a running node and
/// everything running below it; it does nothing to a node that is not
/// running. A node that is not running holds no in-progress state (RateLimit
/// keeps its throttle window on purpose, see there), so its next tick starts
/// it afresh.
///
/// Subclasses implement onTick() and, if they keep in-progress state or start
/// work that must be cancelled, onHalt(). onHalt() must not throw. If
/// onTick() throws, the node is halted (onHalt() is called, which halts its
/// running children) and the exception propagates, so every node on the path
/// to the throwing one is reset and no node is left believing it is running.
class Node {
public:
    explicit Node(std::string name) : name_(std::move(name)) {}
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;
    virtual ~Node() = default;

    /// Ticks the node once. Throws std::logic_error if the node is ticked
    /// again from inside its own tick, or (after halting it) if onTick()
    /// returns a value that is not a Status.
    Status tick(TickContext& context);

    /// Halts the node if it is running: calls onHalt() and sets the state to
    /// Halted. Does nothing otherwise, including when the node is halted from
    /// inside its own tick (the tick decides its state when it returns).
    void halt() noexcept;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] NodeState state() const noexcept { return state_; }
    [[nodiscard]] bool running() const noexcept { return state_ == NodeState::Running; }

    /// The node type, with its parameters, e.g. "Sequence" or "Retry(3)".
    [[nodiscard]] virtual std::string typeName() const = 0;

    [[nodiscard]] virtual std::size_t childCount() const noexcept { return 0; }
    /// Throws std::out_of_range for an index >= childCount().
    [[nodiscard]] virtual const Node& child(std::size_t index) const;

protected:
    /// Does the work of one tick. While it runs, state() still describes the
    /// previous tick, so `running()` tells a continuing node from one that is
    /// starting.
    virtual Status onTick(TickContext& context) = 0;

    /// Abandons in-progress work: halt running children, cancel requests,
    /// reset counters. Called when a running node is halted, and also when
    /// onTick() throws or returns an invalid status. In that case the node
    /// may have been on its first tick (state() Idle or finished), so
    /// cleanup must cope with work that was only partly started.
    virtual void onHalt() noexcept {}

private:
    void abandon() noexcept;

    std::string name_;
    NodeState state_ = NodeState::Idle;
    bool ticking_ = false;
};

using NodePtr = std::unique_ptr<Node>;

}  // namespace mapc::bt
