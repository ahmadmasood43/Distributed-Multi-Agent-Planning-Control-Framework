#pragma once

// Test doubles for behavior tree tests: a leaf whose results are scripted and
// which records every tick and halt in a shared log.

#include "mapc/behavior/blackboard.hpp"
#include "mapc/behavior/node.hpp"
#include "mapc/common/time.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mapc::testing {

/// Ordered record of what happened, e.g. {"tick a", "tick b", "halt a"}.
using EventLog = std::vector<std::string>;

/// A leaf that returns scripted statuses. Each tick takes the next scripted
/// status; when the script is used up it keeps returning `then`.
class ProbeNode final : public bt::Node {
public:
    ProbeNode(std::string name, EventLog* log, bt::Status then = bt::Status::Success)
        : Node(std::move(name)), log_(log), then_(then) {}

    [[nodiscard]] std::string typeName() const override { return "Probe"; }

    /// Statuses for the next ticks, in order.
    ProbeNode& script(std::initializer_list<bt::Status> statuses) {
        script_.insert(script_.end(), statuses.begin(), statuses.end());
        return *this;
    }
    /// What every tick returns once the script is used up.
    ProbeNode& then(bt::Status status) {
        then_ = status;
        return *this;
    }
    /// Called at the start of every tick (e.g. to throw).
    ProbeNode& onEachTick(std::function<void(bt::TickContext&)> hook) {
        hook_ = std::move(hook);
        return *this;
    }

    [[nodiscard]] std::size_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] std::size_t halts() const noexcept { return halts_; }
    [[nodiscard]] const std::vector<Tick>& tickTimes() const noexcept { return tick_times_; }

protected:
    bt::Status onTick(bt::TickContext& context) override {
        ++ticks_;
        tick_times_.push_back(context.now());
        if (log_ != nullptr) {
            log_->push_back("tick " + name());
        }
        if (hook_) {
            hook_(context);
        }
        if (script_.empty()) {
            return then_;
        }
        const bt::Status status = script_.front();
        script_.pop_front();
        return status;
    }

    void onHalt() noexcept override {
        ++halts_;
        if (log_ != nullptr) {
            log_->push_back("halt " + name());
        }
    }

private:
    EventLog* log_;
    bt::Status then_;
    std::deque<bt::Status> script_;
    std::function<void(bt::TickContext&)> hook_;
    std::size_t ticks_ = 0;
    std::size_t halts_ = 0;
    std::vector<Tick> tick_times_;
};

/// A ProbeNode for building trees; `out` keeps a pointer to it for checks.
inline bt::NodePtr probe(std::string name, EventLog* log, ProbeNode*& out,
                         bt::Status then = bt::Status::Success) {
    auto node = std::make_unique<ProbeNode>(std::move(name), log, then);
    out = node.get();
    return node;
}

/// Ticks a node outside a Tree, with its own blackboard.
class Ticker {
public:
    bt::Status tick(bt::Node& node, Tick now) {
        bt::TickContext context(now, blackboard_);
        return node.tick(context);
    }
    bt::Status tick(bt::Node& node, Tick::rep now) { return tick(node, Tick{now}); }

    [[nodiscard]] bt::Blackboard& blackboard() noexcept { return blackboard_; }

private:
    bt::Blackboard blackboard_;
};

}  // namespace mapc::testing
