# Fixture for architecture.parent_project_*: a project that consumes mapc via
# add_subdirectory.
#   CASE=clean                 plain consumer
#   CASE=intermediate_threads  mapc added from a directory that already found
#                              Threads (a directory-scoped imported target)
#   CASE=late_link             adds a ROS dependency to mapc_core after mapc
#                              is configured: outside the guarantee, but it must
#                              show up in the generated requirements
cmake_minimum_required(VERSION 3.21)
project(parent_project LANGUAGES CXX)

if(CASE STREQUAL "intermediate_threads")
    add_subdirectory(intermediate)
else()
    add_subdirectory("${MAPC_SOURCE_DIR}" mapc)
endif()

if(CASE STREQUAL "late_link")
    add_library(rclcpp::rclcpp INTERFACE IMPORTED)
    target_link_libraries(mapc_core PUBLIC rclcpp::rclcpp)
elseif(NOT CASE MATCHES "^(clean|intermediate_threads)$")
    message(FATAL_ERROR "Unknown CASE '${CASE}'")
endif()
