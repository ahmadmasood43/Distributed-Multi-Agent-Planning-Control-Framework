// Fixture for architecture.ros_checker_accepts_clean_tree: project-owned
// namespaces whose names look like ROS packages are fine when nested in mapc
// and declared/used in qualified form.
#pragma once

namespace mapc::agent_msgs {
struct Status {
    int tick = 0;
};
}  // namespace mapc::agent_msgs

namespace mapc {
using AgentStatus = mapc::agent_msgs::Status;
using StatusAlias = ::mapc::agent_msgs::Status;
using SpacedStatus = mapc /* a comment long enough to push the qualifier well back, past any
                            fixed-size lookback window */
    ::agent_msgs::Status;
}  // namespace mapc

inline int rclockTick() {
    return 0;
}  // "rclock" is not the rcl_ prefix
