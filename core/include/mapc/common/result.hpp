#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace mapc {

/// Thrown when a `Result` is accessed for the alternative it does not hold.
class BadResultAccess : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// Wrapper marking a value as the error alternative of a `Result`.
///
/// Wrapping keeps construction unambiguous even when the value and error types
/// are the same (for example `Result<std::string, std::string>`).
template <typename E>
struct Err {
    E error;
};

/// Builds an `Err<E>`; `return err(PlanError::NoPath);` converts to any
/// `Result<T, PlanError>`.
template <typename E>
[[nodiscard]] constexpr Err<std::decay_t<E>> err(E&& error) {
    // String literals deliberately decay to const char* so err("text") works.
    return Err<std::decay_t<E>>{
        std::forward<E>(error)};  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

template <typename T, typename E>
class Result;

namespace detail {

template <typename>
struct IsResult : std::false_type {};
template <typename T, typename E>
struct IsResult<Result<T, E>> : std::true_type {};

template <typename>
struct IsErr : std::false_type {};
template <typename E>
struct IsErr<Err<E>> : std::true_type {};

template <typename T>
inline constexpr bool kIsResultV = IsResult<std::decay_t<T>>::value;
template <typename T>
inline constexpr bool kIsErrV = IsErr<std::decay_t<T>>::value;

[[noreturn]] inline void throwBadResultAccess(const char* what) {
    throw BadResultAccess(what);
}

}  // namespace detail

/// Either a value of type `T` or an error of type `E` (a C++17 subset of
/// `std::expected`).
///
/// Accessing the wrong alternative throws `BadResultAccess`; callers on hot
/// paths are expected to test `hasValue()` first.
///
/// Like `std::variant`, a `Result` can become *valueless* if assigning across
/// alternatives throws part-way (only possible when T or E has a throwing move
/// constructor). In that state both `hasValue()` and `hasError()` are false,
/// both accessors throw, and the object can be reassigned. `!hasValue()`
/// therefore does not by itself imply `hasError()`.
template <typename T, typename E>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>, "Result<T, E>: T must not be a reference");
    static_assert(!std::is_reference_v<E>, "Result<T, E>: E must not be a reference");
    static_assert(!std::is_void_v<E>, "Result<T, E>: E must not be void");
    static_assert(!detail::kIsErrV<T>, "Result<T, E>: T must not be an Err<>");
    static_assert(!detail::kIsResultV<T>, "Result<T, E>: nested Result is not supported");

public:
    using value_type = T;
    using error_type = E;

    /// Implicit construction from anything convertible to `T`.
    template <typename U = T,
              typename = std::enable_if_t<std::is_constructible_v<T, U&&> &&
                                          !detail::kIsResultV<U> && !detail::kIsErrV<U> &&
                                          !std::is_same_v<std::decay_t<U>, std::in_place_t>>>
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    constexpr Result(U&& value) : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

    template <typename... Args>
    constexpr explicit Result(std::in_place_t /*tag*/, Args&&... args)
        : storage_(std::in_place_index<0>, std::forward<Args>(args)...) {}

    /// Implicit construction from an error produced by `err(...)`.
    template <typename G, typename = std::enable_if_t<std::is_constructible_v<E, G&&>>>
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    constexpr Result(Err<G> error)
        : storage_(std::in_place_index<1>, Err<E>{E(std::move(error.error))}) {}

    [[nodiscard]] constexpr bool hasValue() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] constexpr bool hasError() const noexcept { return storage_.index() == 1; }
    constexpr explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] constexpr T& value() & {
        requireValue();
        return *std::get_if<0>(&storage_);
    }
    [[nodiscard]] constexpr const T& value() const& {
        requireValue();
        return *std::get_if<0>(&storage_);
    }
    [[nodiscard]] constexpr T&& value() && {
        requireValue();
        return std::move(*std::get_if<0>(&storage_));
    }

    [[nodiscard]] constexpr E& error() & {
        requireError();
        return std::get_if<1>(&storage_)->error;
    }
    [[nodiscard]] constexpr const E& error() const& {
        requireError();
        return std::get_if<1>(&storage_)->error;
    }
    [[nodiscard]] constexpr E&& error() && {
        requireError();
        return std::move(std::get_if<1>(&storage_)->error);
    }

    constexpr T* operator->() { return std::addressof(value()); }
    constexpr const T* operator->() const { return std::addressof(value()); }
    constexpr T& operator*() & { return value(); }
    constexpr const T& operator*() const& { return value(); }
    constexpr T&& operator*() && { return std::move(*this).value(); }

    template <typename U>
    [[nodiscard]] constexpr T valueOr(U&& fallback) const& {
        return hasValue() ? *std::get_if<0>(&storage_) : static_cast<T>(std::forward<U>(fallback));
    }
    template <typename U>
    [[nodiscard]] constexpr T valueOr(U&& fallback) && {
        return hasValue() ? std::move(*std::get_if<0>(&storage_))
                          : static_cast<T>(std::forward<U>(fallback));
    }

    /// Applies `f` to the value, keeping the error untouched.
    /// `f(T) -> U` gives `Result<U, E>`; `f(T) -> void` gives `Result<void, E>`.
    template <typename F>
    [[nodiscard]] auto map(F&& f) const& {
        return mapImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    [[nodiscard]] auto map(F&& f) && {
        return mapImpl(std::move(*this), std::forward<F>(f));
    }

    /// Chains an operation that can itself fail: `f(T) -> Result<U, E>`.
    template <typename F>
    [[nodiscard]] auto andThen(F&& f) const& {
        return andThenImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    [[nodiscard]] auto andThen(F&& f) && {
        return andThenImpl(std::move(*this), std::forward<F>(f));
    }

    /// Applies `f` to the error, keeping the value untouched: `f(E) -> G`.
    template <typename F>
    [[nodiscard]] auto mapError(F&& f) const& {
        return mapErrorImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    [[nodiscard]] auto mapError(F&& f) && {
        return mapErrorImpl(std::move(*this), std::forward<F>(f));
    }

    friend bool operator==(const Result& a, const Result& b) {
        if (a.storage_.index() != b.storage_.index()) {
            return false;
        }
        if (a.hasValue()) {
            return *std::get_if<0>(&a.storage_) == *std::get_if<0>(&b.storage_);
        }
        if (a.hasError()) {
            return std::get_if<1>(&a.storage_)->error == std::get_if<1>(&b.storage_)->error;
        }
        return true;  // both valueless_by_exception
    }
    friend bool operator!=(const Result& a, const Result& b) { return !(a == b); }

private:
    constexpr void requireValue() const {
        if (!hasValue()) {
            detail::throwBadResultAccess("Result::value() called on a Result holding an error");
        }
    }
    constexpr void requireError() const {
        if (!hasError()) {
            detail::throwBadResultAccess("Result::error() called on a Result holding a value");
        }
    }

    template <typename Self, typename F>
    static auto mapImpl(Self&& self, F&& f) {
        using U = std::invoke_result_t<F, decltype(std::forward<Self>(self).value())>;
        using Out = Result<std::remove_cv_t<U>, E>;
        if (!self.hasValue()) {
            return Out(Err<E>{std::forward<Self>(self).error()});
        }
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
            return Out();
        } else {
            return Out(std::in_place,
                       std::invoke(std::forward<F>(f), std::forward<Self>(self).value()));
        }
    }

    template <typename Self, typename F>
    static auto andThenImpl(Self&& self, F&& f) {
        using Out = std::remove_cv_t<std::remove_reference_t<
            std::invoke_result_t<F, decltype(std::forward<Self>(self).value())>>>;
        static_assert(detail::kIsResultV<Out>, "andThen: the callable must return a Result");
        static_assert(std::is_same_v<typename Out::error_type, E>,
                      "andThen: the callable must return a Result with the same error type");
        if (!self.hasValue()) {
            return Out(Err<E>{std::forward<Self>(self).error()});
        }
        return std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
    }

    template <typename Self, typename F>
    static auto mapErrorImpl(Self&& self, F&& f) {
        using G =
            std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        using Out = Result<T, G>;
        if (self.hasValue()) {
            return Out(std::in_place, std::forward<Self>(self).value());
        }
        return Out(Err<G>{std::invoke(std::forward<F>(f), std::forward<Self>(self).error())});
    }

    std::variant<T, Err<E>> storage_;
};

/// Specialisation for operations that either succeed with no value or fail.
/// A default-constructed `Result<void, E>` represents success.
template <typename E>
class [[nodiscard]] Result<void, E> {
    static_assert(!std::is_reference_v<E>, "Result<void, E>: E must not be a reference");
    static_assert(!std::is_void_v<E>, "Result<void, E>: E must not be void");

public:
    using value_type = void;
    using error_type = E;

    constexpr Result() noexcept = default;

    template <typename G, typename = std::enable_if_t<std::is_constructible_v<E, G&&>>>
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    constexpr Result(Err<G> error) : error_(E(std::move(error.error))) {}

    [[nodiscard]] constexpr bool hasValue() const noexcept { return !error_.has_value(); }
    [[nodiscard]] constexpr bool hasError() const noexcept { return error_.has_value(); }
    constexpr explicit operator bool() const noexcept { return hasValue(); }

    /// Throws `BadResultAccess` if this holds an error; otherwise does nothing.
    constexpr void value() const {
        if (hasError()) {
            detail::throwBadResultAccess("Result::value() called on a Result holding an error");
        }
    }

    [[nodiscard]] constexpr E& error() & {
        if (!error_.has_value()) {
            detail::throwBadResultAccess(kNoErrorMessage);
        }
        return *error_;
    }
    [[nodiscard]] constexpr const E& error() const& {
        if (!error_.has_value()) {
            detail::throwBadResultAccess(kNoErrorMessage);
        }
        return *error_;
    }
    [[nodiscard]] constexpr E&& error() && {
        if (!error_.has_value()) {
            detail::throwBadResultAccess(kNoErrorMessage);
        }
        return std::move(*error_);
    }

    /// `f() -> U` gives `Result<U, E>`; `f() -> void` gives `Result<void, E>`.
    template <typename F>
    [[nodiscard]] auto map(F&& f) const& {
        return mapImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    [[nodiscard]] auto map(F&& f) && {
        return mapImpl(std::move(*this), std::forward<F>(f));
    }

    /// `f() -> Result<U, E>`.
    template <typename F>
    [[nodiscard]] auto andThen(F&& f) const& {
        return andThenImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    [[nodiscard]] auto andThen(F&& f) && {
        return andThenImpl(std::move(*this), std::forward<F>(f));
    }

    template <typename F>
    [[nodiscard]] auto mapError(F&& f) const& {
        return mapErrorImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    [[nodiscard]] auto mapError(F&& f) && {
        return mapErrorImpl(std::move(*this), std::forward<F>(f));
    }

    friend bool operator==(const Result& a, const Result& b) { return a.error_ == b.error_; }
    friend bool operator!=(const Result& a, const Result& b) { return !(a == b); }

private:
    static constexpr const char* kNoErrorMessage =
        "Result::error() called on a Result holding a value";

    template <typename Self, typename F>
    static auto mapImpl(Self&& self, F&& f) {
        using U = std::invoke_result_t<F>;
        using Out = Result<std::remove_cv_t<U>, E>;
        if (self.hasError()) {
            return Out(Err<E>{std::forward<Self>(self).error()});
        }
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f));
            return Out();
        } else {
            return Out(std::in_place, std::invoke(std::forward<F>(f)));
        }
    }

    template <typename Self, typename F>
    static auto andThenImpl(Self&& self, F&& f) {
        using Out = std::remove_cv_t<std::remove_reference_t<std::invoke_result_t<F>>>;
        static_assert(detail::kIsResultV<Out>, "andThen: the callable must return a Result");
        static_assert(std::is_same_v<typename Out::error_type, E>,
                      "andThen: the callable must return a Result with the same error type");
        if (self.hasError()) {
            return Out(Err<E>{std::forward<Self>(self).error()});
        }
        return std::invoke(std::forward<F>(f));
    }

    template <typename Self, typename F>
    static auto mapErrorImpl(Self&& self, F&& f) {
        using G =
            std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        using Out = Result<void, G>;
        if (self.hasValue()) {
            return Out();
        }
        return Out(Err<G>{std::invoke(std::forward<F>(f), std::forward<Self>(self).error())});
    }

    std::optional<E> error_;
};

}  // namespace mapc
