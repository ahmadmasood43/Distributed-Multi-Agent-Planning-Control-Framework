#include "mapc/world/task.hpp"

#include "mapc/world/agent_snapshot.hpp"
#include "mapc/world/capabilities.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace mapc {
namespace {

TEST(CapabilitySetTest, StartsEmpty) {
    const CapabilitySet set;
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.bits(), 0U);
    EXPECT_FALSE(set.contains(0));
}

TEST(CapabilitySetTest, OfAndAddSetBits) {
    CapabilitySet set = CapabilitySet::of({0, 5, 63});
    EXPECT_EQ(set.bits(),
              (std::uint64_t{1} << 0) | (std::uint64_t{1} << 5) | (std::uint64_t{1} << 63));
    EXPECT_TRUE(set.contains(5));
    EXPECT_FALSE(set.contains(6));
    set.add(6).add(6);
    EXPECT_TRUE(set.contains(6));
    EXPECT_EQ(set, CapabilitySet::fromBits(set.bits()));
    EXPECT_NE(set, CapabilitySet{});
}

TEST(CapabilitySetTest, RejectsIndicesBeyondSixtyFour) {
    EXPECT_THROW((void)CapabilitySet::of({64}), std::out_of_range);
    CapabilitySet set;
    EXPECT_THROW(set.add(CapabilitySet::kMaxCapabilities), std::out_of_range);
    EXPECT_THROW((void)set.contains(100), std::out_of_range);
    EXPECT_TRUE(set.empty());
}

TEST(CapabilitySetTest, ContainsAllIsSubsetTest) {
    const CapabilitySet agent = CapabilitySet::of({1, 2, 3});
    EXPECT_TRUE(agent.containsAll(CapabilitySet{}));
    EXPECT_TRUE(agent.containsAll(CapabilitySet::of({1, 3})));
    EXPECT_TRUE(agent.containsAll(agent));
    EXPECT_FALSE(agent.containsAll(CapabilitySet::of({1, 4})));
    EXPECT_FALSE(CapabilitySet{}.containsAll(CapabilitySet::of({0})));
}

TEST(TaskTest, DefaultsArePendingWithoutRequirements) {
    const Task task;
    EXPECT_FALSE(task.id.valid());
    EXPECT_EQ(task.priority, 0);
    EXPECT_FALSE(task.deadline.has_value());
    EXPECT_TRUE(task.requirements.capabilities.empty());
    EXPECT_EQ(task.status, TaskStatus::Pending);

    const AgentSnapshot agent;
    EXPECT_FALSE(agent.current_task.valid());
}

TEST(TaskTest, StatusNames) {
    EXPECT_EQ(toString(TaskStatus::Pending), "pending");
    EXPECT_EQ(toString(TaskStatus::Assigned), "assigned");
    EXPECT_EQ(toString(TaskStatus::InProgress), "in progress");
    EXPECT_EQ(toString(TaskStatus::Done), "done");
    EXPECT_EQ(toString(TaskStatus::Failed), "failed");
    EXPECT_EQ(toString(TaskStatus::Cancelled), "cancelled");
    EXPECT_EQ(toString(static_cast<TaskStatus>(99)), "unknown status");
}

}  // namespace
}  // namespace mapc
