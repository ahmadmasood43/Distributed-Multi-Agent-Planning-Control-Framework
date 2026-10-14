// Core-like translation unit for architecture.header_deps_detects_real_includes.
// It reaches a stub ROS header only through a symlink with an innocent name
// (created at configure time), included via a macro.
#define MAPC_FIXTURE_ALIAS <mapc/alias.hpp>
#include MAPC_FIXTURE_ALIAS

int fixtureCore() {
    return rclcppStub_e();
}
