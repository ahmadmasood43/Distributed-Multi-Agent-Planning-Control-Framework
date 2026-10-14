// Fixture for architecture.ros_checker_accepts_clean_tree: none of these
// lines is an active ROS include.
// clang-format off
int x; /*
*/ #include <rclcpp/not_a_directive.hpp>
// disabled include \
#include <rclcpp/spliced_into_comment.hpp>
const char* kSpliced = "hello\
#include <rclcpp/inside_string.hpp>\
world";
const char* kRaw = u8R"foo(
#include <rclcpp/inside_raw_string.hpp>
)foo";
// clang-format on
