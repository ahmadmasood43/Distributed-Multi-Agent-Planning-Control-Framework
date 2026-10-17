#pragma once

#include "mapc/behavior/node.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mapc::bt {

/// A node with one or more children, ticked in order.
class Composite : public Node {
public:
    [[nodiscard]] std::size_t childCount() const noexcept override { return children_.size(); }
    [[nodiscard]] const Node& child(std::size_t index) const override;

protected:
    /// Throws std::invalid_argument if `children` is empty or holds a null.
    Composite(std::string name, std::vector<NodePtr> children);

    [[nodiscard]] Node& childAt(std::size_t index) noexcept { return *children_[index]; }

    /// Halts every running child with an index >= `first`.
    void haltChildren(std::size_t first = 0) noexcept;

private:
    std::vector<NodePtr> children_;
};

/// Ticks its children in order until one fails. Succeeds when all have
/// succeeded; fails as soon as one fails.
///
/// It has memory: when a child returns Running, the next tick resumes at that
/// child, and the children before it are not ticked again. Use
/// ReactiveSequence when earlier conditions must be rechecked every tick.
class Sequence final : public Composite {
public:
    Sequence(std::string name, std::vector<NodePtr> children);
    [[nodiscard]] std::string typeName() const override { return "Sequence"; }

protected:
    Status onTick(TickContext& context) override;
    void onHalt() noexcept override;

private:
    std::size_t current_ = 0;
};

/// Ticks its children in order until one succeeds (a "selector"). Fails when
/// all have failed; succeeds as soon as one succeeds. Like Sequence, it
/// resumes at a Running child.
class Fallback final : public Composite {
public:
    Fallback(std::string name, std::vector<NodePtr> children);
    [[nodiscard]] std::string typeName() const override { return "Fallback"; }

protected:
    Status onTick(TickContext& context) override;
    void onHalt() noexcept override;

private:
    std::size_t current_ = 0;
};

/// A Sequence without memory: every tick starts again at the first child, so
/// conditions in front of a running action are rechecked each tick. When a
/// child fails or is Running, every child after it that is still running
/// (from an earlier tick) is halted.
///
/// Typical use: [PathStillValid?, FollowPath] stops following the path in the
/// tick the path becomes invalid.
class ReactiveSequence final : public Composite {
public:
    ReactiveSequence(std::string name, std::vector<NodePtr> children);
    [[nodiscard]] std::string typeName() const override { return "ReactiveSequence"; }

protected:
    Status onTick(TickContext& context) override;
    void onHalt() noexcept override;
};

/// A Fallback without memory: every tick starts again at the first child, so
/// a higher-priority branch preempts a lower one as soon as it stops failing.
/// When a child succeeds or is Running, every child after it that is still
/// running is halted.
///
/// Typical use: a [FaultDetected?, Recover] branch in front of the normal
/// task branch interrupts the task the tick a fault appears.
class ReactiveFallback final : public Composite {
public:
    ReactiveFallback(std::string name, std::vector<NodePtr> children);
    [[nodiscard]] std::string typeName() const override { return "ReactiveFallback"; }

protected:
    Status onTick(TickContext& context) override;
    void onHalt() noexcept override;
};

/// Runs all children together: succeeds once `success_threshold` of them have
/// succeeded, fails once so many have failed that the threshold can no longer
/// be reached. Children are ticked in order, and the decision is made as soon
/// as it is certain, so later children may not be ticked in that tick. A
/// child that has finished is not ticked again until the Parallel finishes or
/// is halted; children still running at that point are halted.
class Parallel final : public Composite {
public:
    /// Throws std::invalid_argument unless 1 <= success_threshold <= number
    /// of children.
    Parallel(std::string name, std::vector<NodePtr> children, std::size_t success_threshold);

    [[nodiscard]] std::string typeName() const override;
    [[nodiscard]] std::size_t successThreshold() const noexcept { return success_threshold_; }

protected:
    Status onTick(TickContext& context) override;
    void onHalt() noexcept override;

private:
    void reset() noexcept;

    std::size_t success_threshold_;
    std::vector<Status> results_;  // per child; Running = not finished yet
    std::size_t successes_ = 0;
    std::size_t failures_ = 0;
};

}  // namespace mapc::bt
