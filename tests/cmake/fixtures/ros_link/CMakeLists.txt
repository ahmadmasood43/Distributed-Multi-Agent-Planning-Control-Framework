# Fixture for architecture.ros_checker_detects_link_dependency (never
# configured): linking a ROS target without an explicit find_package here. The
# quoted "#" must not be mistaken for a comment that hides the rest.
add_library(fixture STATIC fixture.cpp)
target_link_libraries(fixture
    PUBLIC
        "legit#lib"
        rclcpp::rclcpp)
