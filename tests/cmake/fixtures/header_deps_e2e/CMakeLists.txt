# Fixture project for architecture.header_deps_detects_real_includes.
#
# core_like is built from core.cpp plus the objects of an object library, the
# way a modular core could be. Both paths reach stub ROS headers:
#  - component (obfuscated.cpp) includes them with obfuscated directives;
#  - core.cpp includes one through a symlink named mapc/alias.hpp.
# symbols.cpp calls ROS functions declared by hand, without any ROS header.
# owned/core_owned.cpp includes an .ipp file and a generated header that use
# ROS only in inline code that is never instantiated.
cmake_minimum_required(VERSION 3.21)
project(header_deps_e2e LANGUAGES CXX)

set(_alias_dir "${CMAKE_CURRENT_BINARY_DIR}/alias_include/mapc")
file(MAKE_DIRECTORY "${_alias_dir}")
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/fake_ros/include/rclcpp/e.hpp"
     "${_alias_dir}/alias.hpp" SYMBOLIC)

add_library(component OBJECT obfuscated.cpp)
target_include_directories(component PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/fake_ros/include")

# A header generated at configure time, like mapc/version.hpp.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/mapc/generated_probe.hpp"
"#pragma once
extern \"C\" int rcl_init(int argc, const char* const* argv, void* options, void* context);
inline int generatedProbe() { return rcl_init(0, nullptr, nullptr, nullptr); }
")

add_library(core_like STATIC core.cpp symbols.cpp owned/core_owned.cpp $<TARGET_OBJECTS:component>)
target_include_directories(core_like PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/alias_include"
    "${CMAKE_CURRENT_SOURCE_DIR}/owned"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

# A shared core-like library that is clean at the source level but links a
# (stub) ROS library through legacy LINK_FLAGS: the dependency exists only as
# DT_NEEDED, with no ROS symbol left undefined.
add_library(fake_rclcpp SHARED fake_rclcpp.cpp)
set_target_properties(fake_rclcpp PROPERTIES OUTPUT_NAME rclcpp)
add_library(core_shared SHARED core_shared.cpp)
add_dependencies(core_shared fake_rclcpp)
set_property(TARGET core_shared PROPERTY LINK_FLAGS
    "-Wl,--no-as-needed -L${CMAKE_CURRENT_BINARY_DIR} -lrclcpp")
