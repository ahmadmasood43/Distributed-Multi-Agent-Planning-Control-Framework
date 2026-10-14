// Fixture for architecture.ros_checker_detects_include_between_comments.
// clang-format off
// The loader recognizes opening delimiter /*
#include <rclcpp/rclcpp.hpp>
// and closing delimiter */
/* a block comment before the directive */ #include <std_msgs/msg/string.hpp>
const char* kSlashes = "//";  // a string that looks like a comment
#include <rcutils/logging.h>
const char* kOpen = "/*";
#include <rmw/rmw.h>
const char* kClose = "*/";
// clang-format on
