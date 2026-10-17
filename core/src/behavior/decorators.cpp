#include "mapc/behavior/decorators.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace mapc::bt {

namespace {

std::string withParameter(const char* type, std::int64_t parameter) {
    return std::string(type) + "(" + std::to_string(parameter) + ")";
}

}  // namespace

// --- Decorator ------------------------------------------------------------------

Decorator::Decorator(std::string name, NodePtr child)
    : Node(std::move(name)), child_(std::move(child)) {
    if (!child_) {
        throw std::invalid_argument("bt decorator '" + this->name() + "': child is null");
    }
}

const Node& Decorator::child(std::size_t index) const {
    if (index != 0) {
        return Node::child(index);
    }
    return *child_;
}

void Decorator::onHalt() noexcept {
    child_->halt();
}

// --- Inverter -------------------------------------------------------------------

Inverter::Inverter(NodePtr child, std::string name)
    : Decorator(std::move(name), std::move(child)) {}

Status Inverter::onTick(TickContext& context) {
    switch (wrapped().tick(context)) {
        case Status::Success:
            return Status::Failure;
        case Status::Failure:
            return Status::Success;
        case Status::Running:
            break;
    }
    return Status::Running;
}

// --- Retry ----------------------------------------------------------------------

Retry::Retry(std::uint32_t max_attempts, NodePtr child, std::string name)
    : Decorator(std::move(name), std::move(child)), max_attempts_(max_attempts) {
    if (max_attempts == 0) {
        throw std::invalid_argument("bt::Retry needs at least one attempt");
    }
}

std::string Retry::typeName() const {
    return withParameter("Retry", max_attempts_);
}

Status Retry::onTick(TickContext& context) {
    const Status status = wrapped().tick(context);
    if (status == Status::Running) {
        return Status::Running;
    }
    if (status == Status::Success) {
        failed_ = 0;
        return Status::Success;
    }
    ++failed_;
    if (failed_ >= max_attempts_) {
        failed_ = 0;
        return Status::Failure;
    }
    return Status::Running;  // the next attempt starts on the next tick
}

void Retry::onHalt() noexcept {
    Decorator::onHalt();
    failed_ = 0;
}

// --- Timeout --------------------------------------------------------------------

Timeout::Timeout(Duration limit, NodePtr child, std::string name)
    : Decorator(std::move(name), std::move(child)), limit_(limit) {
    if (limit < Duration{1}) {
        throw std::invalid_argument("bt::Timeout needs a limit of at least 1 tick");
    }
}

std::string Timeout::typeName() const {
    return withParameter("Timeout", limit_.count());
}

Status Timeout::onTick(TickContext& context) {
    if (!running()) {
        start_ = context.now();
    }
    if (context.now() - start_ >= limit_) {
        wrapped().halt();
        return Status::Failure;
    }
    return wrapped().tick(context);
}

// --- RateLimit ------------------------------------------------------------------

RateLimit::RateLimit(Duration period, NodePtr child, std::string name)
    : Decorator(std::move(name), std::move(child)), period_(period) {
    if (period < Duration{1}) {
        throw std::invalid_argument("bt::RateLimit needs a period of at least 1 tick");
    }
}

std::string RateLimit::typeName() const {
    return withParameter("RateLimit", period_.count());
}

Status RateLimit::onTick(TickContext& context) {
    if (!wrapped().running()) {
        if (last_start_ && context.now() - *last_start_ < period_) {
            return last_result_;
        }
        last_start_ = context.now();
    }
    const Status status = wrapped().tick(context);
    if (status != Status::Running) {
        last_result_ = status;
    }
    return status;
}

}  // namespace mapc::bt
