# Fixture for architecture.ros_checker_accepts_clean_tree (never configured).
# find_package(rclcpp REQUIRED)  -- commented out, so not a dependency
#[[
target_link_libraries(fixture PUBLIC rclcpp::rclcpp)
]]
#[=[
find_package(rclcpp REQUIRED)  # ]] inside a level-1 bracket comment
]=]
message(STATUS "find_package(rclcpp REQUIRED) is only allowed in adapters/ros2")
message(STATUS [[target_link_libraries(x rclcpp::rclcpp)]])
my_find_package(rclcpp)  # a different command that merely ends in find_package
find_package(Threads REQUIRED)
target_link_libraries(fixture PUBLIC Threads::Threads)
message(STATUS find_package(rclcpp REQUIRED))
message(STATUS target_link_libraries(core PUBLIC rclcpp::rclcpp))
