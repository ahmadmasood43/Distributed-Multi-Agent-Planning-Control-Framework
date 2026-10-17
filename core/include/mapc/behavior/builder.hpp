#pragma once

#include "mapc/behavior/composites.hpp"
#include "mapc/behavior/decorators.hpp"
#include "mapc/behavior/leaves.hpp"
#include "mapc/behavior/node.hpp"
#include "mapc/common/time.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/// A small C++ DSL for composing trees. The shape of the code follows the
/// shape of the tree:
///
///     using namespace mapc::bt;
///     Tree tree(reactiveFallback("agent",
///         sequence("recover",
///             condition("fault?", [&](TickContext&) { return world.faulted(); }),
///             action("recover", recover)),
///         sequence("work",
///             condition("has task?", hasTask),
///             retry(3, action("plan", plan)),
///             action("follow path", follow)),
///         action("idle", idle)));
///
/// Each function returns a NodePtr, so subtrees can also be built by separate
/// functions and passed in. Children may be any std::unique_ptr to a Node
/// type, and composites also accept a std::vector<NodePtr> built at run time.
namespace mapc::bt {

namespace detail {

template <typename... Children>
std::vector<NodePtr> collectChildren(Children&&... children) {
    static_assert((std::is_convertible_v<Children&&, NodePtr> && ...),
                  "children must be NodePtr (std::unique_ptr<Node>) rvalues");
    std::vector<NodePtr> nodes;
    nodes.reserve(sizeof...(children));
    (nodes.emplace_back(std::forward<Children>(children)), ...);
    return nodes;
}

}  // namespace detail

/// Any node type, constructed from `args`.
template <typename NodeType, typename... Args>
[[nodiscard]] NodePtr node(Args&&... args) {
    static_assert(std::is_base_of_v<Node, NodeType>, "node<T>: T must derive from bt::Node");
    return std::make_unique<NodeType>(std::forward<Args>(args)...);
}

// --- Composites ---------------------------------------------------------------

[[nodiscard]] inline NodePtr sequence(std::string name, std::vector<NodePtr> children) {
    return std::make_unique<Sequence>(std::move(name), std::move(children));
}
template <typename... Children>
[[nodiscard]] NodePtr sequence(std::string name, Children&&... children) {
    return sequence(std::move(name), detail::collectChildren(std::forward<Children>(children)...));
}

[[nodiscard]] inline NodePtr fallback(std::string name, std::vector<NodePtr> children) {
    return std::make_unique<Fallback>(std::move(name), std::move(children));
}
template <typename... Children>
[[nodiscard]] NodePtr fallback(std::string name, Children&&... children) {
    return fallback(std::move(name), detail::collectChildren(std::forward<Children>(children)...));
}

[[nodiscard]] inline NodePtr reactiveSequence(std::string name, std::vector<NodePtr> children) {
    return std::make_unique<ReactiveSequence>(std::move(name), std::move(children));
}
template <typename... Children>
[[nodiscard]] NodePtr reactiveSequence(std::string name, Children&&... children) {
    return reactiveSequence(std::move(name),
                            detail::collectChildren(std::forward<Children>(children)...));
}

[[nodiscard]] inline NodePtr reactiveFallback(std::string name, std::vector<NodePtr> children) {
    return std::make_unique<ReactiveFallback>(std::move(name), std::move(children));
}
template <typename... Children>
[[nodiscard]] NodePtr reactiveFallback(std::string name, Children&&... children) {
    return reactiveFallback(std::move(name),
                            detail::collectChildren(std::forward<Children>(children)...));
}

[[nodiscard]] inline NodePtr parallel(std::string name, std::size_t success_threshold,
                                      std::vector<NodePtr> children) {
    return std::make_unique<Parallel>(std::move(name), std::move(children), success_threshold);
}
template <typename... Children>
[[nodiscard]] NodePtr parallel(std::string name, std::size_t success_threshold,
                               Children&&... children) {
    return parallel(std::move(name), success_threshold,
                    detail::collectChildren(std::forward<Children>(children)...));
}

// --- Decorators ---------------------------------------------------------------

[[nodiscard]] inline NodePtr inverter(NodePtr child) {
    return std::make_unique<Inverter>(std::move(child));
}

[[nodiscard]] inline NodePtr retry(std::uint32_t max_attempts, NodePtr child) {
    return std::make_unique<Retry>(max_attempts, std::move(child));
}

[[nodiscard]] inline NodePtr timeout(Duration limit, NodePtr child) {
    return std::make_unique<Timeout>(limit, std::move(child));
}

[[nodiscard]] inline NodePtr rateLimit(Duration period, NodePtr child) {
    return std::make_unique<RateLimit>(period, std::move(child));
}

// --- Leaves -------------------------------------------------------------------

[[nodiscard]] inline NodePtr condition(std::string name, Condition::Predicate predicate) {
    return std::make_unique<Condition>(std::move(name), std::move(predicate));
}

[[nodiscard]] inline NodePtr action(std::string name, Action::TickFunction on_tick,
                                    Action::HaltFunction on_halt = {}) {
    return std::make_unique<Action>(std::move(name), std::move(on_tick), std::move(on_halt));
}

}  // namespace mapc::bt
