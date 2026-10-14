// Fixture for architecture.header_deps_detects_real_includes: each directive
// below really includes a (stub) ROS header, spelled so that a text scanner
// would struggle. The compiler's dependency record sees all of them.
// clang-format off
#/**/include <rclcpp/a.hpp>
#inc\
lude <rclcpp/b.hpp>
#define MAPC_FIXTURE_HEADER <rclcpp/c.hpp>
#include MAPC_FIXTURE_HEADER
%:include <rclcpp/d.hpp>
// clang-format on

int fixtureUsesStubs() {
    return rclcppStub_a() + rclcppStub_b() + rclcppStub_c() + rclcppStub_d();
}
