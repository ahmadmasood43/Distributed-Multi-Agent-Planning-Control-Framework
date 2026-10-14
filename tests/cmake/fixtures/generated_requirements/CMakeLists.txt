# Fixture for architecture.generated_requirements_*: the configure-time check
# runs and passes, then CASE adds a dependency through a channel that runs
# *after* it. Only the generate-time dump can see the final state.
cmake_minimum_required(VERSION 3.21)
project(generated_requirements LANGUAGES CXX)

include("${MAPC_SOURCE_DIR}/cmake/ArchitectureRules.cmake")

add_library(rclcpp::rclcpp INTERFACE IMPORTED)
add_library(warnings INTERFACE)
target_compile_options(warnings INTERFACE -Wall)
add_library(core STATIC lib.cpp)
target_link_libraries(core PRIVATE warnings)

mapc_defer_dependency_check(core
    ALLOWED_TARGETS warnings
    ALLOWED_LINK_ITEMS pthread
    ALLOWED_SOURCE_ROOTS "${CMAKE_CURRENT_SOURCE_DIR}"
    DUMP_FILE "${CMAKE_CURRENT_BINARY_DIR}/core.requirements.cmake")

if(CASE STREQUAL "deferred_link")
    # Queued after the check, so it runs after it.
    cmake_language(DEFER CALL target_link_libraries core PUBLIC rclcpp::rclcpp)
elseif(CASE STREQUAL "deferred_via_allowed")
    cmake_language(DEFER CALL target_link_libraries warnings INTERFACE rclcpp::rclcpp)
elseif(CASE STREQUAL "deferred_link_flags")
    cmake_language(DEFER CALL set_property TARGET core PROPERTY LINK_FLAGS "-L/opt/ros/jazzy/lib -lrclcpp")
elseif(CASE STREQUAL "deferred_direct_link")
    cmake_language(DEFER CALL set_property TARGET core PROPERTY INTERFACE_LINK_LIBRARIES_DIRECT rclcpp::rclcpp)
elseif(CASE STREQUAL "late_plain_target")
    # A target created later under an allowed *plain* name, with its own
    # dependencies, then linked: its usage requirements were never inspected.
    function(create_late_target)
        add_library(pthread INTERFACE)
        target_link_libraries(pthread INTERFACE rclcpp::rclcpp)
        target_link_libraries(core PUBLIC pthread)
    endfunction()
    cmake_language(DEFER CALL create_late_target)
elseif(NOT CASE STREQUAL "clean")
    message(FATAL_ERROR "Unknown CASE '${CASE}'")
endif()
