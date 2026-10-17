#include "mapc/behavior/leaves.hpp"

#include <stdexcept>
#include <utility>

namespace mapc::bt {

Condition::Condition(std::string name, Predicate predicate)
    : Node(std::move(name)), predicate_(std::move(predicate)) {
    if (!predicate_) {
        throw std::invalid_argument("bt::Condition '" + this->name() + "' has no predicate");
    }
}

Status Condition::onTick(TickContext& context) {
    return predicate_(context) ? Status::Success : Status::Failure;
}

Action::Action(std::string name, TickFunction on_tick, HaltFunction on_halt)
    : Node(std::move(name)), on_tick_(std::move(on_tick)), on_halt_(std::move(on_halt)) {
    if (!on_tick_) {
        throw std::invalid_argument("bt::Action '" + this->name() + "' has no tick function");
    }
}

Status Action::onTick(TickContext& context) {
    return on_tick_(context);
}

void Action::onHalt() noexcept {
    if (on_halt_) {
        on_halt_();
    }
}

Status StatefulAction::onTick(TickContext& context) {
    return running() ? onRunning(context) : onStart(context);
}

}  // namespace mapc::bt
