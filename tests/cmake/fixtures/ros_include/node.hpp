// Fixture for architecture.ros_checker_detects_include: core code must never
// include ROS headers, so the checker has to reject this file. The unusual
// spacing of the second include is part of the test.
#pragma once

// clang-format off
#include <rclcpp/rclcpp.hpp>
#  include "geometry_msgs/msg/pose.hpp"
// clang-format on
