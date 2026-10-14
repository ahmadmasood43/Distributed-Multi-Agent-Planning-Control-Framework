// Fixture for architecture.ros_checker_accepts_clean_tree: mentions of ROS in
// comments or literals are not dependencies and must not be flagged.
#pragma once

// clang-format off
#include <string>

// #include <rclcpp/rclcpp.hpp>   (commented-out code is not a dependency)
/*
#include <std_msgs/msg/string.hpp>
*/
inline const std::string kNote = "find_package(rclcpp) is only allowed in adapters/ros2";
inline const char* kRaw = R"ros(
#include <rclcpp/rclcpp.hpp>
)ros";
inline const char kQuote = '"';
inline constexpr long kBig = 1'000'000;  // digit separators are not char literals
inline const char* kMsg = "#include <rclcpp/rclcpp.hpp>";
// clang-format on
