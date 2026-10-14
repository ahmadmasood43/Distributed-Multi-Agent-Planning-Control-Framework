#include "mapc/common/strong_id.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mapc {
namespace {

// --- Compile-time type-safety guarantees -----------------------------------

static_assert(!std::is_same_v<AgentId, NodeId>);
static_assert(!std::is_convertible_v<AgentId, NodeId>, "ids of different kinds must not mix");
static_assert(!std::is_constructible_v<NodeId, AgentId>);
static_assert(!std::is_assignable_v<NodeId&, AgentId>);
static_assert(!std::is_convertible_v<std::uint32_t, AgentId>, "construction must be explicit");
static_assert(!std::is_convertible_v<AgentId, std::uint32_t>, "no implicit unwrapping");
static_assert(!std::is_convertible_v<AgentId, bool>, "no implicit truthiness");
static_assert(std::is_constructible_v<AgentId, std::uint32_t>);
static_assert(std::is_trivially_copyable_v<AgentId>);
static_assert(sizeof(AgentId) == sizeof(std::uint32_t), "zero-overhead wrapper");
static_assert(std::is_nothrow_default_constructible_v<AgentId>);

// Usable in constant expressions.
static_assert(!AgentId{}.valid());
static_assert(AgentId{7}.value() == 7);
static_assert(AgentId{1} < AgentId{2});
static_assert(AgentId::invalid() == AgentId{});

struct SmallTag {
    static constexpr const char* kName = "small";
};
using SmallId = StrongId<SmallTag, std::uint8_t>;

struct WideTag {
    static constexpr const char* kName = "wide";
};
using WideId = StrongId<WideTag, std::uint64_t>;

static_assert(SmallId::kInvalidValue == 255);
static_assert(WideId::kInvalidValue == std::numeric_limits<std::uint64_t>::max());

// --- Runtime behaviour -----------------------------------------------------

TEST(StrongIdTest, DefaultConstructedIsInvalid) {
    const AgentId id;
    EXPECT_FALSE(id.valid());
    EXPECT_EQ(id, AgentId::invalid());
    EXPECT_EQ(id.value(), AgentId::kInvalidValue);
}

TEST(StrongIdTest, ZeroIsAValidId) {
    const NodeId id{0};
    EXPECT_TRUE(id.valid());
    EXPECT_EQ(id.value(), 0U);
}

TEST(StrongIdTest, LargestNonSentinelValueIsValid) {
    const NodeId id{NodeId::kInvalidValue - 1};
    EXPECT_TRUE(id.valid());
}

TEST(StrongIdTest, ExplicitSentinelValueIsInvalid) {
    const NodeId id{NodeId::kInvalidValue};
    EXPECT_FALSE(id.valid());
    EXPECT_EQ(id, NodeId::invalid());
}

TEST(StrongIdTest, ComparisonOperatorsFollowUnderlyingValue) {
    const TaskId a{1};
    const TaskId b{2};
    EXPECT_TRUE(a == TaskId{1});
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(a <= TaskId{1});
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(b >= a);
    EXPECT_TRUE(b >= TaskId{2});
    EXPECT_FALSE(a == b);
    EXPECT_FALSE(b < a);
}

TEST(StrongIdTest, InvalidSortsAfterEveryValidId) {
    EXPECT_LT(AgentId{0}, AgentId::invalid());
    EXPECT_LT(AgentId{AgentId::kInvalidValue - 1}, AgentId::invalid());
}

TEST(StrongIdTest, WorksAsKeyInHashedContainers) {
    std::unordered_map<AgentId, int> map;
    map[AgentId{3}] = 30;
    map[AgentId{4}] = 40;
    map[AgentId{3}] += 1;
    EXPECT_EQ(map.size(), 2U);
    EXPECT_EQ(map.at(AgentId{3}), 31);

    const std::unordered_set<NodeId> set{NodeId{1}, NodeId{1}, NodeId{2}, NodeId::invalid()};
    EXPECT_EQ(set.size(), 3U);
    EXPECT_EQ(set.count(NodeId::invalid()), 1U);
}

TEST(StrongIdTest, WorksAsKeyInOrderedContainers) {
    const std::set<EdgeId> set{EdgeId{5}, EdgeId{1}, EdgeId{3}};
    std::vector<EdgeId::rep_type> order;
    order.reserve(set.size());
    for (const auto id : set) {
        order.push_back(id.value());
    }
    EXPECT_EQ(order, (std::vector<EdgeId::rep_type>{1, 3, 5}));

    const std::map<ResourceId, int> map{{ResourceId{2}, 0}};
    EXPECT_EQ(map.count(ResourceId{2}), 1U);
}

TEST(StrongIdTest, HashMatchesUnderlyingHash) {
    EXPECT_EQ(std::hash<AgentId>{}(AgentId{42}), std::hash<std::uint32_t>{}(42U));
}

TEST(StrongIdTest, StreamsWithKindPrefix) {
    std::ostringstream os;
    os << AgentId{3} << ' ' << NodeId{0} << ' ' << TaskId::invalid();
    EXPECT_EQ(os.str(), "agent#3 node#0 task#invalid");
}

TEST(StrongIdTest, ToStringMatchesStreamOutput) {
    EXPECT_EQ(toString(ResourceId{12}), "resource#12");
    EXPECT_EQ(toString(EdgeId::invalid()), "edge#invalid");
}

TEST(StrongIdTest, EightBitRepresentationPrintsAsNumberNotCharacter) {
    std::ostringstream os;
    os << SmallId{65};  // would print 'A' without promotion
    EXPECT_EQ(os.str(), "small#65");
    EXPECT_EQ(toString(SmallId{65}), "small#65");
    EXPECT_EQ(toString(SmallId{}), "small#invalid");
    EXPECT_TRUE(SmallId{254}.valid());
}

TEST(StrongIdTest, SixtyFourBitRepresentationRoundTrips) {
    const WideId id{std::numeric_limits<std::uint64_t>::max() - 1};
    EXPECT_TRUE(id.valid());
    EXPECT_EQ(toString(id), "wide#18446744073709551614");
}

}  // namespace
}  // namespace mapc
