# Fixture for architecture.ros_checker_accepts_clean_tree: command-looking text
# nested inside another command's arguments, across lines, is not a command.
message(STATUS
  find_package(rclcpp REQUIRED)
)
message(STATUS
  target_link_libraries(core PUBLIC rclcpp::rclcpp))
