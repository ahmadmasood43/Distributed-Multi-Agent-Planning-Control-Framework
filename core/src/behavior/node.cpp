#include "mapc/behavior/node.hpp"

#include <stdexcept>
#include <string>

namespace mapc::bt {

namespace {

// Clears the re-entrancy flag however the tick ends.
class TickGuard {
public:
    explicit TickGuard(bool& flag) noexcept : flag_(&flag) { *flag_ = true; }
    TickGuard(const TickGuard&) = delete;
    TickGuard& operator=(const TickGuard&) = delete;
    TickGuard(TickGuard&&) = delete;
    TickGuard& operator=(TickGuard&&) = delete;
    ~TickGuard() { *flag_ = false; }

private:
    bool* flag_;
};

}  // namespace

std::string_view toString(Status status) noexcept {
    switch (status) {
        case Status::Success:
            return "Success";
        case Status::Failure:
            return "Failure";
        case Status::Running:
            return "Running";
    }
    return "unknown status";
}

std::string_view toString(NodeState state) noexcept {
    switch (state) {
        case NodeState::Idle:
            return "Idle";
        case NodeState::Running:
            return "Running";
        case NodeState::Success:
            return "Success";
        case NodeState::Failure:
            return "Failure";
        case NodeState::Halted:
            return "Halted";
    }
    return "unknown state";
}

Status Node::tick(TickContext& context) {
    if (ticking_) {
        throw std::logic_error("bt::Node '" + name_ + "' was ticked from inside its own tick");
    }
    Status status = Status::Failure;
    try {
        const TickGuard guard(ticking_);
        status = onTick(context);
    } catch (...) {
        // Children ticked in this tick may be running, and composites may
        // have advanced; halting resets all of that before unwinding further.
        abandon();
        throw;
    }
    switch (status) {
        case Status::Success:
            state_ = NodeState::Success;
            return status;
        case Status::Failure:
            state_ = NodeState::Failure;
            return status;
        case Status::Running:
            state_ = NodeState::Running;
            return status;
    }
    abandon();
    throw std::logic_error("bt::Node '" + name_ + "' returned an invalid status");
}

void Node::abandon() noexcept {
    onHalt();
    state_ = NodeState::Halted;
}

void Node::halt() noexcept {
    // A node halted from inside its own tick is left alone: its tick is still
    // on the stack and decides the state when it returns.
    if (state_ != NodeState::Running || ticking_) {
        return;
    }
    abandon();
}

const Node& Node::child(std::size_t index) const {
    throw std::out_of_range("bt::Node '" + name_ + "' has no child " + std::to_string(index));
}

}  // namespace mapc::bt
