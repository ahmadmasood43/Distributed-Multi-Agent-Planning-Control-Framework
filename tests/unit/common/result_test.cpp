#include "mapc/common/result.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mapc {
namespace {

enum class ParseError : std::uint8_t { Empty, NotANumber };

Result<int, ParseError> parseDigit(const std::string& text) {
    if (text.empty()) {
        return err(ParseError::Empty);
    }
    if (text.size() != 1 || text[0] < '0' || text[0] > '9') {
        return err(ParseError::NotANumber);
    }
    return text[0] - '0';
}

// --- Compile-time properties -------------------------------------------------

static_assert(std::is_convertible_v<int, Result<int, ParseError>>, "values convert implicitly");
static_assert(std::is_convertible_v<Err<ParseError>, Result<int, ParseError>>);
static_assert(!std::is_convertible_v<ParseError, Result<int, ParseError>>,
              "a bare error must be wrapped with err() so intent is explicit");
static_assert(!std::is_default_constructible_v<Result<int, ParseError>>,
              "a Result<T, E> must be explicitly a value or an error");
static_assert(std::is_default_constructible_v<Result<void, ParseError>>);
static_assert(!std::is_copy_constructible_v<Result<std::unique_ptr<int>, ParseError>>);
static_assert(std::is_move_constructible_v<Result<std::unique_ptr<int>, ParseError>>);

// --- Construction and access -------------------------------------------------

TEST(ResultTest, HoldsValue) {
    const auto r = parseDigit("7");
    ASSERT_TRUE(r.hasValue());
    EXPECT_FALSE(r.hasError());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 7);
    EXPECT_EQ(*r, 7);
}

TEST(ResultTest, HoldsError) {
    const auto r = parseDigit("");
    ASSERT_TRUE(r.hasError());
    EXPECT_FALSE(r.hasValue());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), ParseError::Empty);
}

TEST(ResultTest, AccessingWrongAlternativeThrows) {
    const auto bad = parseDigit("x");
    EXPECT_THROW((void)bad.value(), BadResultAccess);
    EXPECT_THROW((void)*bad, BadResultAccess);

    const auto good = parseDigit("1");
    EXPECT_THROW((void)good.error(), BadResultAccess);
}

TEST(ResultTest, BadResultAccessIsALogicError) {
    try {
        (void)parseDigit("").value();
        FAIL() << "expected BadResultAccess";
    } catch (const std::logic_error& e) {
        EXPECT_NE(std::string(e.what()).find("error"), std::string::npos);
    }
}

TEST(ResultTest, SameValueAndErrorTypeIsUnambiguous) {
    const Result<std::string, std::string> ok = std::string("payload");
    const Result<std::string, std::string> bad = err(std::string("reason"));
    EXPECT_TRUE(ok.hasValue());
    EXPECT_EQ(ok.value(), "payload");
    EXPECT_TRUE(bad.hasError());
    EXPECT_EQ(bad.error(), "reason");
    EXPECT_NE(ok, bad);
}

TEST(ResultTest, ErrorConvertsFromCompatibleType) {
    const Result<int, std::string> r = err("literal");  // const char* -> std::string
    EXPECT_EQ(r.error(), "literal");
}

TEST(ResultTest, InPlaceConstruction) {
    const Result<std::vector<int>, ParseError> r(std::in_place, 3, 9);
    EXPECT_EQ(r.value(), (std::vector<int>{9, 9, 9}));
}

TEST(ResultTest, NonDefaultConstructibleValue) {
    struct NoDefault {
        explicit NoDefault(int v) : value(v) {}
        int value;
    };
    const Result<NoDefault, ParseError> r(std::in_place, 5);
    EXPECT_EQ(r->value, 5);
    const Result<NoDefault, ParseError> e = err(ParseError::Empty);
    EXPECT_TRUE(e.hasError());
}

/// A legal but hostile type: its unary operator& does not return its address.
struct HostileAddress {
    int payload = 3;
    const HostileAddress* operator&() const { return nullptr; }  // NOLINT(google-runtime-operator)
};

TEST(ResultTest, ArrowIgnoresOverloadedAddressOf) {
    const Result<HostileAddress, ParseError> r = HostileAddress{};
    ASSERT_NE(r.operator->(), nullptr);
    EXPECT_EQ(r->payload, 3);
    Result<HostileAddress, ParseError> mutable_r = HostileAddress{};
    mutable_r->payload = 4;
    EXPECT_EQ(mutable_r.value().payload, 4);
}

TEST(ResultTest, MoveOnlyValueCanBeMovedOut) {
    Result<std::unique_ptr<int>, ParseError> r = std::make_unique<int>(11);
    const std::unique_ptr<int> owned = std::move(r).value();
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(*owned, 11);
}

TEST(ResultTest, MoveOnlyErrorCanBeMovedOut) {
    Result<int, std::unique_ptr<std::string>> r = err(std::make_unique<std::string>("boom"));
    auto owned = std::move(r).error();
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(*owned, "boom");
}

TEST(ResultTest, MutableAccessModifiesInPlace) {
    Result<std::string, ParseError> r = std::string("ab");
    r.value() += "c";
    r->push_back('d');
    EXPECT_EQ(*r, "abcd");

    Result<int, std::string> e = err(std::string("x"));
    e.error() += "y";
    EXPECT_EQ(e.error(), "xy");
}

TEST(ResultTest, CopyAndAssignmentSwitchAlternatives) {
    Result<int, ParseError> r = 1;
    const Result<int, ParseError> error = err(ParseError::Empty);
    r = error;
    EXPECT_TRUE(r.hasError());
    r = 4;
    EXPECT_EQ(r.value(), 4);
}

/// Copying succeeds or throws on demand; used to drive std::variant into its
/// valueless_by_exception state. The move constructor must be potentially
/// throwing: otherwise std::variant copies into a temporary first and gives
/// assignment the strong guarantee, so it never becomes valueless.
struct FragileCopy {
    explicit FragileCopy(bool throw_on_copy) : throws(throw_on_copy) {}
    FragileCopy(const FragileCopy& other) : throws(other.throws) {
        if (throws) {
            throw std::runtime_error("copy failed");
        }
    }
    FragileCopy& operator=(const FragileCopy&) = default;
    // NOLINTNEXTLINE(performance-noexcept-move-constructor)
    FragileCopy(FragileCopy&& other) noexcept(false) : throws(other.throws) {}
    FragileCopy& operator=(FragileCopy&&) = default;
    ~FragileCopy() = default;
    bool operator==(const FragileCopy& other) const { return throws == other.throws; }
    bool throws;
};

TEST(ResultTest, ValuelessAfterThrowingAssignmentIsDetected) {
    Result<int, FragileCopy> target = 1;
    const Result<int, FragileCopy> source = err(FragileCopy{false});
    // Make the stored error throw on its next copy, then assign across
    // alternatives: the variant destroys the int and the copy throws.
    Result<int, FragileCopy> poison = err(FragileCopy{false});
    poison.error().throws = true;
    EXPECT_THROW(target = poison, std::runtime_error);

    EXPECT_FALSE(target.hasValue());
    EXPECT_FALSE(target.hasError());
    EXPECT_FALSE(static_cast<bool>(target));
    EXPECT_THROW((void)target.value(), BadResultAccess);
    EXPECT_THROW((void)target.error(), BadResultAccess);

    // Two valueless results compare equal; valueless never equals a live one.
    Result<int, FragileCopy> other = 2;
    EXPECT_THROW(other = poison, std::runtime_error);
    EXPECT_EQ(target, other);
    EXPECT_NE(target, source);

    // A valueless result can be reassigned and becomes usable again. Copy
    // assignment is used on purpose: move-assigning a type whose move may throw
    // is exactly what the fixture is built to break.
    const Result<int, FragileCopy> five = 5;
    target = five;
    EXPECT_EQ(target.value(), 5);
}

TEST(ResultTest, RvalueErrorAccessOnValueThrows) {
    Result<int, std::string> r = 3;
    EXPECT_THROW((void)std::move(r).error(), BadResultAccess);
    Result<int, std::string> e = err(std::string("x"));
    EXPECT_THROW((void)std::move(e).value(), BadResultAccess);
}

TEST(ResultTest, ValueOrReturnsFallbackOnlyOnError) {
    EXPECT_EQ(parseDigit("3").valueOr(-1), 3);
    EXPECT_EQ(parseDigit("").valueOr(-1), -1);

    Result<std::unique_ptr<int>, ParseError> moved = std::make_unique<int>(2);
    auto p = std::move(moved).valueOr(nullptr);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 2);
}

TEST(ResultTest, EqualityComparesAlternativeThenContent) {
    EXPECT_EQ(parseDigit("1"), parseDigit("1"));
    EXPECT_NE(parseDigit("1"), parseDigit("2"));
    EXPECT_EQ(parseDigit(""), parseDigit(""));
    EXPECT_NE(parseDigit(""), parseDigit("x"));
    EXPECT_NE(parseDigit("1"), parseDigit(""));
}

// --- Monadic operations ------------------------------------------------------

TEST(ResultTest, MapTransformsValueAndPropagatesError) {
    const auto doubled = parseDigit("4").map([](int v) { return v * 2; });
    EXPECT_EQ(doubled.value(), 8);

    const auto as_text = parseDigit("4").map([](int v) { return std::to_string(v); });
    static_assert(std::is_same_v<std::decay_t<decltype(as_text)>, Result<std::string, ParseError>>);
    EXPECT_EQ(as_text.value(), "4");

    bool called = false;
    const auto failed = parseDigit("").map([&](int v) {
        called = true;
        return v;
    });
    EXPECT_FALSE(called);
    EXPECT_EQ(failed.error(), ParseError::Empty);
}

TEST(ResultTest, MapWithVoidCallableYieldsVoidResult) {
    int seen = 0;
    const auto r = parseDigit("6").map([&](int v) { seen = v; });
    static_assert(std::is_same_v<std::decay_t<decltype(r)>, Result<void, ParseError>>);
    EXPECT_TRUE(r.hasValue());
    EXPECT_EQ(seen, 6);
}

TEST(ResultTest, MapOnRvalueMovesValue) {
    Result<std::unique_ptr<int>, ParseError> r = std::make_unique<int>(3);
    auto mapped = std::move(r).map([](std::unique_ptr<int> p) { return *p + 1; });
    EXPECT_EQ(mapped.value(), 4);
}

TEST(ResultTest, AndThenChainsFallibleSteps) {
    const auto half = [](int v) -> Result<int, ParseError> {
        if (v % 2 != 0) {
            return err(ParseError::NotANumber);
        }
        return v / 2;
    };
    EXPECT_EQ(parseDigit("8").andThen(half).andThen(half).value(), 2);
    EXPECT_EQ(parseDigit("6").andThen(half).andThen(half).error(), ParseError::NotANumber);
    EXPECT_EQ(parseDigit("").andThen(half).error(), ParseError::Empty);
}

TEST(ResultTest, MapErrorTransformsErrorOnly) {
    const auto to_text = [](ParseError e) {
        return e == ParseError::Empty ? std::string("empty") : std::string("nan");
    };
    const auto bad = parseDigit("").mapError(to_text);
    static_assert(std::is_same_v<std::decay_t<decltype(bad)>, Result<int, std::string>>);
    EXPECT_EQ(bad.error(), "empty");

    const auto good = parseDigit("5").mapError(to_text);
    EXPECT_EQ(good.value(), 5);
}

// --- Result<void, E> ---------------------------------------------------------

Result<void, std::string> validate(int v) {
    if (v < 0) {
        return err(std::string("negative"));
    }
    return {};
}

TEST(VoidResultTest, DefaultIsSuccess) {
    const Result<void, ParseError> r;
    EXPECT_TRUE(r.hasValue());
    EXPECT_NO_THROW(r.value());
    EXPECT_THROW((void)r.error(), BadResultAccess);
}

TEST(VoidResultTest, ErrorState) {
    const auto r = validate(-1);
    EXPECT_TRUE(r.hasError());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), "negative");
    EXPECT_THROW(r.value(), BadResultAccess);
}

TEST(VoidResultTest, MapAndThenAndMapError) {
    EXPECT_EQ(validate(1).map([] { return 42; }).value(), 42);
    EXPECT_EQ(validate(-1).map([] { return 42; }).error(), "negative");

    int calls = 0;
    EXPECT_TRUE(validate(1).map([&] { ++calls; }).hasValue());
    EXPECT_EQ(calls, 1);

    const auto chained = validate(1).andThen(
        []() -> Result<void, std::string> { return err(std::string("second step")); });
    EXPECT_EQ(chained.error(), "second step");
    EXPECT_EQ(validate(-1).andThen([] { return Result<void, std::string>{}; }).error(), "negative");

    const auto sized = validate(-1).mapError([](const std::string& s) { return s.size(); });
    EXPECT_EQ(sized.error(), 8U);
    EXPECT_TRUE(validate(0).mapError([](const std::string& s) { return s.size(); }).hasValue());
}

TEST(VoidResultTest, ErrorAccessOverloads) {
    Result<void, std::string> e = err(std::string("abc"));
    e.error() += "d";
    EXPECT_EQ(std::as_const(e).error(), "abcd");
    const std::string moved = std::move(e).error();
    EXPECT_EQ(moved, "abcd");

    Result<void, std::string> ok;
    EXPECT_THROW((void)ok.error(), BadResultAccess);
    EXPECT_THROW((void)std::as_const(ok).error(), BadResultAccess);
    EXPECT_THROW((void)std::move(ok).error(), BadResultAccess);
}

TEST(VoidResultTest, RvalueMonadicOperations) {
    Result<void, std::unique_ptr<int>> bad = err(std::make_unique<int>(7));
    auto mapped = std::move(bad).map([] { return 1; });
    ASSERT_TRUE(mapped.hasError());
    EXPECT_EQ(*mapped.error(), 7);

    Result<void, std::unique_ptr<int>> bad2 = err(std::make_unique<int>(8));
    auto chained = std::move(bad2).andThen([] { return Result<int, std::unique_ptr<int>>(1); });
    ASSERT_TRUE(chained.hasError());
    EXPECT_EQ(*chained.error(), 8);

    Result<void, std::unique_ptr<int>> bad3 = err(std::make_unique<int>(9));
    auto converted = std::move(bad3).mapError([](std::unique_ptr<int> p) { return *p * 2; });
    EXPECT_EQ(converted.error(), 18);

    Result<void, std::unique_ptr<int>> good;
    EXPECT_EQ(std::move(good).andThen([] { return Result<int, std::unique_ptr<int>>(4); }).value(),
              4);
}

TEST(VoidResultTest, Equality) {
    EXPECT_EQ(validate(1), validate(2));
    EXPECT_EQ(validate(-1), validate(-2));
    EXPECT_NE(validate(1), validate(-1));
}

}  // namespace
}  // namespace mapc
