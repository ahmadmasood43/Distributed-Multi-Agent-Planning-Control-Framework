#pragma once

#include "mapc/behavior/blackboard.hpp"
#include "mapc/behavior/node.hpp"
#include "mapc/common/time.hpp"

#include <optional>
#include <string>

namespace mapc::bt {

/// A behavior tree: a root node and the blackboard its nodes share.
///
/// The tree is ticked with the simulation time, which must never go
/// backwards. If a node throws during a tick, the nodes on the path to it are
/// halted as the exception propagates (see Node), so the tree is left with
/// nothing running. Destroying the tree halts it, so running actions get
/// their halt callbacks. Not thread-safe; tick each tree from one thread.
class Tree {
public:
    /// Throws std::invalid_argument if `root` is null.
    explicit Tree(NodePtr root);
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    Tree(Tree&&) = delete;
    Tree& operator=(Tree&&) = delete;
    ~Tree();

    /// Ticks the root once at time `now`. Throws std::invalid_argument if
    /// `now` is earlier than the previous tick, and std::logic_error if
    /// called from inside a tick of this tree.
    Status tick(Tick now);

    /// Halts every running node. Throws std::logic_error if called from
    /// inside a tick of this tree.
    void halt();

    [[nodiscard]] const Node& root() const noexcept { return *root_; }
    [[nodiscard]] Blackboard& blackboard() noexcept { return blackboard_; }
    [[nodiscard]] const Blackboard& blackboard() const noexcept { return blackboard_; }

    /// The time of the last tick, if there has been one.
    [[nodiscard]] std::optional<Tick> lastTick() const noexcept { return last_tick_; }

    /// dumpTree(root()).
    [[nodiscard]] std::string dump() const;

private:
    NodePtr root_;
    Blackboard blackboard_;
    std::optional<Tick> last_tick_;
    bool ticking_ = false;
};

/// The tree under `root` as indented ASCII text, one node per line, with its
/// type, name and state:
///
///     ReactiveFallback "agent" [Running]
///     +-- Sequence "recover" [Failure]
///     |   +-- Condition "fault?" [Failure]
///     |   `-- Action "recover" [Idle]
///     `-- Action "idle" [Running]
[[nodiscard]] std::string dumpTree(const Node& root);

}  // namespace mapc::bt
