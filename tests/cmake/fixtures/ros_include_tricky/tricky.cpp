// Fixture for architecture.ros_checker_detects_obfuscated_includes: every
// directive below compiles and really includes a ROS header.
// clang-format off
#/**/include <rclcpp/a.hpp>
#include/**/<rclcpp/b.hpp>
#include \
<rclcpp/c.hpp>
#inc\
lude <rclcpp/d.hpp>
%:include <rclcpp/e.hpp>
#include <rclcpp/f.hpp>
const auto kQuote = L'"';
#include <rclcpp/g.hpp>
#define PREFIX_R ""
const char* kText = PREFIX_R "(";
#include <rclcpp/h.hpp>
// clang-format on
