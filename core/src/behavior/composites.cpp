#include "mapc/behavior/composites.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace mapc::bt {

// --- Composite ----------------------------------------------------------------

Composite::Composite(std::string name, std::vector<NodePtr> children)
    : Node(std::move(name)), children_(std::move(children)) {
    if (children_.empty()) {
        throw std::invalid_argument("bt composite '" + this->name() + "' needs at least one child");
    }
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (!children_[i]) {
            throw std::invalid_argument("bt composite '" + this->name() + "': child " +
                                        std::to_string(i) + " is null");
        }
    }
}

const Node& Composite::child(std::size_t index) const {
    if (index >= children_.size()) {
        return Node::child(index);
    }
    return *children_[index];
}

void Composite::haltChildren(std::size_t first) noexcept {
    for (std::size_t i = first; i < children_.size(); ++i) {
        children_[i]->halt();
    }
}

// --- Sequence / Fallback --------------------------------------------------------

Sequence::Sequence(std::string name, std::vector<NodePtr> children)
    : Composite(std::move(name), std::move(children)) {}

Status Sequence::onTick(TickContext& context) {
    while (current_ < childCount()) {
        const Status status = childAt(current_).tick(context);
        if (status == Status::Running) {
            return Status::Running;
        }
        if (status == Status::Failure) {
            current_ = 0;
            return Status::Failure;
        }
        ++current_;
    }
    current_ = 0;
    return Status::Success;
}

void Sequence::onHalt() noexcept {
    haltChildren();
    current_ = 0;
}

Fallback::Fallback(std::string name, std::vector<NodePtr> children)
    : Composite(std::move(name), std::move(children)) {}

Status Fallback::onTick(TickContext& context) {
    while (current_ < childCount()) {
        const Status status = childAt(current_).tick(context);
        if (status == Status::Running) {
            return Status::Running;
        }
        if (status == Status::Success) {
            current_ = 0;
            return Status::Success;
        }
        ++current_;
    }
    current_ = 0;
    return Status::Failure;
}

void Fallback::onHalt() noexcept {
    haltChildren();
    current_ = 0;
}

// --- Reactive variants ----------------------------------------------------------

ReactiveSequence::ReactiveSequence(std::string name, std::vector<NodePtr> children)
    : Composite(std::move(name), std::move(children)) {}

Status ReactiveSequence::onTick(TickContext& context) {
    for (std::size_t i = 0; i < childCount(); ++i) {
        const Status status = childAt(i).tick(context);
        if (status != Status::Success) {
            // A child further on may still be running from an earlier tick.
            haltChildren(i + 1);
            return status;
        }
    }
    return Status::Success;
}

void ReactiveSequence::onHalt() noexcept {
    haltChildren();
}

ReactiveFallback::ReactiveFallback(std::string name, std::vector<NodePtr> children)
    : Composite(std::move(name), std::move(children)) {}

Status ReactiveFallback::onTick(TickContext& context) {
    for (std::size_t i = 0; i < childCount(); ++i) {
        const Status status = childAt(i).tick(context);
        if (status != Status::Failure) {
            // A lower-priority child may still be running from an earlier tick.
            haltChildren(i + 1);
            return status;
        }
    }
    return Status::Failure;
}

void ReactiveFallback::onHalt() noexcept {
    haltChildren();
}

// --- Parallel -------------------------------------------------------------------

Parallel::Parallel(std::string name, std::vector<NodePtr> children, std::size_t success_threshold)
    : Composite(std::move(name), std::move(children)),
      success_threshold_(success_threshold),
      results_(childCount(), Status::Running) {
    if (success_threshold == 0 || success_threshold > childCount()) {
        throw std::invalid_argument("bt::Parallel '" + this->name() + "': success threshold " +
                                    std::to_string(success_threshold) + " is not in [1, " +
                                    std::to_string(childCount()) + "]");
    }
}

std::string Parallel::typeName() const {
    return "Parallel(" + std::to_string(success_threshold_) + "/" + std::to_string(childCount()) +
           ")";
}

Status Parallel::onTick(TickContext& context) {
    const std::size_t max_failures = childCount() - success_threshold_;
    for (std::size_t i = 0; i < childCount(); ++i) {
        if (results_[i] != Status::Running) {
            continue;  // finished earlier in this run
        }
        const Status status = childAt(i).tick(context);
        if (status == Status::Running) {
            continue;
        }
        results_[i] = status;
        if (status == Status::Success) {
            ++successes_;
        } else {
            ++failures_;
        }
        if (successes_ >= success_threshold_ || failures_ > max_failures) {
            const Status outcome =
                successes_ >= success_threshold_ ? Status::Success : Status::Failure;
            haltChildren();
            reset();
            return outcome;
        }
    }
    return Status::Running;
}

void Parallel::onHalt() noexcept {
    haltChildren();
    reset();
}

void Parallel::reset() noexcept {
    results_.assign(results_.size(), Status::Running);
    successes_ = 0;
    failures_ = 0;
}

}  // namespace mapc::bt
