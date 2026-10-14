# ROS name patterns shared by the repository checks in this directory.
#
# MAPC_ROS_PACKAGE_REGEX     ROS 2 client libraries, middleware, generated
#                            interface packages and common ROS-only libraries,
#                            as lower-case package names.
# MAPC_ROS_C_PREFIX_REGEX    Prefix of ROS C API functions and variables
#                            (rcl_init, rcutils_..., g_rcutils_...).
set(MAPC_ROS_PACKAGE_REGEX "(rclcpp[a-z_]*|rclpy|rcl|rcl_[a-z_]+|rcutils|rcpputils|rmw[a-z_]*|rosidl_[a-z_]+|ament_[a-z_]+|tf2[a-z_]*|message_filters|image_transport|[a-z0-9_]+_msgs|[a-z0-9_]+_srvs|[a-z0-9_]+_interfaces)")
set(MAPC_ROS_C_PREFIX_REGEX "(g_)?(rcl|rcl_action|rcl_lifecycle|rcl_logging|rcl_yaml_param_parser|rmw|rmw_dds_common|rcutils|rosidl|rosidl_typesupport|rosidl_runtime_c|tracetools|rcpputils)_")
