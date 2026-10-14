// Fixture for architecture.ros_checker_detects_ros_identifiers: ROS used
// without any ROS header, in code the core library itself never instantiates.
#pragma once

extern "C" int rcl_init(int argc, const char* const* argv, void* options, void* context);
extern "C" bool g_rcutils_logging_initialized;

namespace std_msgs::msg {
struct String;
}  // namespace std_msgs::msg

inline int startRos() {
    return rcl_init(0, nullptr, nullptr, nullptr);
}

template <typename T>
bool initMessage(T* message) {
    return std_msgs__msg__String__init(message) && g_rcutils_logging_initialized;
}

template <typename Node = rclcpp::Node>
struct Wrapper {};
