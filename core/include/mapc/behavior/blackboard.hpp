#pragma once

#include <any>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace mapc::bt {

/// The name of a blackboard entry together with the type stored under it.
///
/// Keys are usually declared once, next to the nodes that use them:
///
///     constexpr BlackboardKey<NodeId> kGoal{"goal"};
///
/// A key only refers to its name (it does not copy it), so the name must
/// outlive the key; string literals always do.
template <typename T>
class BlackboardKey {
public:
    static_assert(std::is_same_v<T, std::decay_t<T>>,
                  "blackboard values are stored by value: use a plain object type");
    static_assert(std::is_copy_constructible_v<T>,
                  "blackboard values are stored in std::any, which needs copyable types");

    using value_type = T;

    constexpr explicit BlackboardKey(std::string_view name) noexcept : name_(name) {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return name_; }

private:
    std::string_view name_;
};

/// Thrown when an entry is read or written as a different type than the one
/// stored under its name.
class BlackboardTypeError : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// Named, typed values shared by the nodes of a tree.
///
/// Values are stored in std::any, and every access goes through a
/// BlackboardKey<T>, so a value is always read back as the type it was stored
/// as: a mismatch throws BlackboardTypeError instead of returning garbage.
/// Once a name holds a type, it keeps that type until it is erased.
///
/// References and pointers to a value stay valid until its entry is erased or
/// the blackboard is cleared or destroyed; setting the entry again assigns to
/// the stored object in place. Not thread-safe.
class Blackboard {
public:
    /// Stores `value` under the key's name. Throws BlackboardTypeError if the
    /// name already holds a value of another type.
    template <typename T>
    void set(const BlackboardKey<T>& key, T value) {
        const auto it = entries_.find(key.name());
        if (it == entries_.end()) {
            entries_.emplace(std::string(key.name()), std::any(std::move(value)));
            return;
        }
        *checkedCast<T>(it->first, it->second) = std::move(value);
    }

    /// The value under the key's name, or nullptr if there is none. Throws
    /// BlackboardTypeError if the name holds a value of another type.
    template <typename T>
    [[nodiscard]] T* find(const BlackboardKey<T>& key) {
        const auto it = entries_.find(key.name());
        return it == entries_.end() ? nullptr : checkedCast<T>(it->first, it->second);
    }
    template <typename T>
    [[nodiscard]] const T* find(const BlackboardKey<T>& key) const {
        const auto it = entries_.find(key.name());
        return it == entries_.end() ? nullptr : checkedCast<T>(it->first, it->second);
    }

    /// The value under the key's name. Throws std::out_of_range if there is
    /// none, and BlackboardTypeError if it has another type.
    template <typename T>
    [[nodiscard]] T& get(const BlackboardKey<T>& key) {
        T* value = find(key);
        if (value == nullptr) {
            throwMissing(key.name());
        }
        return *value;
    }
    template <typename T>
    [[nodiscard]] const T& get(const BlackboardKey<T>& key) const {
        const T* value = find(key);
        if (value == nullptr) {
            throwMissing(key.name());
        }
        return *value;
    }

    /// A copy of the value under the key's name, or `fallback` if there is
    /// none. Throws BlackboardTypeError if it has another type.
    template <typename T>
    [[nodiscard]] T valueOr(const BlackboardKey<T>& key, T fallback) const {
        const T* value = find(key);
        return value == nullptr ? std::move(fallback) : *value;
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return entries_.find(name) != entries_.end();
    }

    /// Removes the entry; returns false if there was none.
    bool erase(std::string_view name);

    void clear() noexcept { entries_.clear(); }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    /// The names of all entries, in lexicographic order.
    [[nodiscard]] std::vector<std::string> names() const;

private:
    template <typename T>
    [[nodiscard]] static T* checkedCast(const std::string& name, std::any& value) {
        if (value.type() != typeid(T)) {
            throwTypeMismatch(name, typeid(T), value.type());
        }
        return std::any_cast<T>(&value);
    }
    template <typename T>
    [[nodiscard]] static const T* checkedCast(const std::string& name, const std::any& value) {
        if (value.type() != typeid(T)) {
            throwTypeMismatch(name, typeid(T), value.type());
        }
        return std::any_cast<T>(&value);
    }

    [[noreturn]] static void throwMissing(std::string_view name);
    [[noreturn]] static void throwTypeMismatch(std::string_view name,
                                               const std::type_info& requested,
                                               const std::type_info& stored);

    // Ordered, so names() and dumps are deterministic; std::less<> allows
    // lookups by string_view without allocating.
    std::map<std::string, std::any, std::less<>> entries_;
};

}  // namespace mapc::bt
