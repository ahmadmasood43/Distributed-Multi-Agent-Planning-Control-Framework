#pragma once

#include "mapc/behavior/node.hpp"

#include <functional>
#include <string>

namespace mapc::bt {

/// A leaf that tests something: Success if the predicate is true, Failure
/// otherwise. Never Running.
class Condition final : public Node {
public:
    using Predicate = std::function<bool(TickContext&)>;

    /// Throws std::invalid_argument if `predicate` is empty.
    Condition(std::string name, Predicate predicate);
    [[nodiscard]] std::string typeName() const override { return "Condition"; }

protected:
    Status onTick(TickContext& context) override;

private:
    Predicate predicate_;
};

/// A leaf that does something, given as a function.
///
/// The function is called on every tick and returns the action's status. An
/// action that takes several ticks returns Running until it is done; give it
/// an `on_halt` callback if it starts work that must be stopped when it is
/// halted. `on_halt` must not throw. It is also called if the tick function
/// throws (see Node::onHalt), so it must cope with work that was only partly
/// started. For an action that needs to tell its first tick from later ones,
/// derive from StatefulAction instead.
class Action final : public Node {
public:
    using TickFunction = std::function<Status(TickContext&)>;
    using HaltFunction = std::function<void()>;

    /// Throws std::invalid_argument if `on_tick` is empty.
    Action(std::string name, TickFunction on_tick, HaltFunction on_halt = {});
    [[nodiscard]] std::string typeName() const override { return "Action"; }

protected:
    Status onTick(TickContext& context) override;
    void onHalt() noexcept override;

private:
    TickFunction on_tick_;
    HaltFunction on_halt_;
};

/// Base class for actions with a start, a continuation and a cancellation,
/// such as following a path or waiting for a planning result.
///
/// onStart() is called on the first tick of a run, onRunning() on each later
/// tick while the action is running, and onHalted() when it is halted while
/// running. onHalted() is also called if onStart() or onRunning() throws
/// (see Node::onHalt), so it must cope with a start that did not finish.
class StatefulAction : public Node {
public:
    using Node::Node;
    [[nodiscard]] std::string typeName() const override { return "Action"; }

protected:
    virtual Status onStart(TickContext& context) = 0;
    virtual Status onRunning(TickContext& context) = 0;
    virtual void onHalted() noexcept {}

private:
    Status onTick(TickContext& context) final;
    void onHalt() noexcept final { onHalted(); }
};

}  // namespace mapc::bt
