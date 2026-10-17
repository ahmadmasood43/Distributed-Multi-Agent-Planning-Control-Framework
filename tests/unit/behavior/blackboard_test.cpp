#include "mapc/behavior/blackboard.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace mapc::bt {
namespace {

constexpr BlackboardKey<int> kCount{"count"};
constexpr BlackboardKey<std::string> kLabel{"label"};
constexpr BlackboardKey<double> kCountAsDouble{"count"};  // same name, other type

TEST(BlackboardTest, StoresAndReadsTypedValues) {
    Blackboard bb;
    EXPECT_TRUE(bb.empty());
    bb.set(kCount, 3);
    bb.set(kLabel, std::string("dock"));
    EXPECT_EQ(bb.get(kCount), 3);
    EXPECT_EQ(bb.get(kLabel), "dock");
    EXPECT_EQ(bb.size(), 2U);
    EXPECT_TRUE(bb.contains("count"));
    EXPECT_FALSE(bb.contains("missing"));
}

TEST(BlackboardTest, MissingEntries) {
    Blackboard bb;
    EXPECT_EQ(bb.find(kCount), nullptr);
    EXPECT_THROW((void)bb.get(kCount), std::out_of_range);
    EXPECT_EQ(bb.valueOr(kCount, 7), 7);
    const Blackboard& cbb = bb;
    EXPECT_EQ(cbb.find(kCount), nullptr);
    EXPECT_THROW((void)cbb.get(kCount), std::out_of_range);
}

TEST(BlackboardTest, ANameKeepsItsTypeUntilErased) {
    Blackboard bb;
    bb.set(kCount, 3);
    EXPECT_THROW(bb.set(kCountAsDouble, 1.5), BlackboardTypeError);
    EXPECT_THROW((void)bb.get(kCountAsDouble), BlackboardTypeError);
    EXPECT_THROW((void)bb.find(kCountAsDouble), BlackboardTypeError);
    EXPECT_THROW((void)bb.valueOr(kCountAsDouble, 0.0), BlackboardTypeError);
    EXPECT_EQ(bb.get(kCount), 3);  // unchanged by the failed set

    EXPECT_TRUE(bb.erase("count"));
    EXPECT_FALSE(bb.erase("count"));
    bb.set(kCountAsDouble, 1.5);
    EXPECT_EQ(bb.get(kCountAsDouble), 1.5);
}

TEST(BlackboardTest, SettingAgainAssignsInPlace) {
    Blackboard bb;
    bb.set(kLabel, std::string("a"));
    const std::string* before = bb.find(kLabel);
    bb.set(kLabel, std::string("b"));
    EXPECT_EQ(bb.find(kLabel), before);  // references stay valid
    EXPECT_EQ(*before, "b");
}

TEST(BlackboardTest, ValuesCanBeModifiedThroughGet) {
    Blackboard bb;
    bb.set(kCount, 1);
    ++bb.get(kCount);
    *bb.find(kCount) += 10;
    EXPECT_EQ(bb.get(kCount), 12);
}

TEST(BlackboardTest, NamesAreSortedAndClearEmpties) {
    Blackboard bb;
    bb.set(kLabel, std::string("x"));
    bb.set(kCount, 1);
    bb.set(BlackboardKey<bool>{"armed"}, true);
    EXPECT_EQ(bb.names(), (std::vector<std::string>{"armed", "count", "label"}));
    bb.clear();
    EXPECT_TRUE(bb.empty());
    EXPECT_TRUE(bb.names().empty());
}

TEST(BlackboardTest, KeysWithRuntimeNamesWork) {
    Blackboard bb;
    const std::string name = "agent-" + std::to_string(4);
    bb.set(BlackboardKey<int>{name}, 4);
    EXPECT_EQ(bb.get(BlackboardKey<int>{"agent-4"}), 4);
}

}  // namespace
}  // namespace mapc::bt
