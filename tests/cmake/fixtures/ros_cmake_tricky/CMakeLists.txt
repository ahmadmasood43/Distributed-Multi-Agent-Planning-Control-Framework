# Fixture for architecture.ros_checker_detects_obfuscated_cmake_dependencies
# (never configured). Each command below adds a real ROS dependency.
target_link_libraries(core INTERFACE harmless\#tag rclcpp::rclcpp)
target_link_libraries(core INTERFACE harmless\)tag rmw::rmw)
target_link_libraries(core PRIVATE "optional(name)" rcutils::rcutils)
ament_target_dependencies(core SYSTEM std_msgs)
find_package([=[geometry_msgs]=] REQUIRED)
