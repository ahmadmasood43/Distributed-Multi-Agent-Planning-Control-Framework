# Self-checking fixture for cmake/ArchitectureRules.cmake, configured by the
# architecture.dependency_allowlist test. Configuration fails unless every
# target below is classified exactly as expected.
cmake_minimum_required(VERSION 3.21)
project(dependency_rules LANGUAGES CXX)

include("${MAPC_SOURCE_DIR}/cmake/ArchitectureRules.cmake")

set(_root "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(_ros_include "${CMAKE_CURRENT_SOURCE_DIR}/fake_ros/include")
add_library(rclcpp::rclcpp INTERFACE IMPORTED)
add_library(allowed::dep INTERFACE IMPORTED)

# Mimics ament_target_dependencies: links the package and adds its includes.
function(fake_ament_target_dependencies target)
    foreach(_pkg IN LISTS ARGN)
        target_link_libraries(${target} PUBLIC ${_pkg}::${_pkg})
        target_include_directories(${target} SYSTEM PUBLIC "${_ros_include}")
    endforeach()
endfunction()

set(_failures 0)

# expect(<target> <expected violation count> [<regex that must appear>])
function(expect target count)
    mapc_check_target_dependencies(${target}
        ALLOWED_TARGETS allowed::dep allowed_local scoped::dep
        ALLOWED_LINK_ITEMS -pthread plainlib
        ALLOWED_LINK_OPTIONS -fsanitize=address
        ALLOWED_INCLUDE_ROOTS "${_root}"
        ALLOWED_INSTALL_INCLUDES include
        ALLOWED_SOURCE_ROOTS "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _violations)
    list(LENGTH _violations _n)
    set(_ok TRUE)
    if(NOT _n EQUAL count)
        set(_ok FALSE)
    endif()
    if(ARGC GREATER 2 AND NOT _violations MATCHES "${ARGV2}")
        set(_ok FALSE)
    endif()
    if(_ok)
        message(STATUS "ok   ${target}: ${_n} violation(s)")
    else()
        message(SEND_ERROR "FAIL ${target}: expected ${count} violation(s) matching '${ARGV2}', got ${_n}: ${_violations}")
    endif()
endfunction()

# A locally defined allowed target (like mapc_warnings) with clean usage
# requirements.
add_library(allowed_local INTERFACE)
target_compile_options(allowed_local INTERFACE -Wall -Wextra)

# Clean: allowed link (also as a private dependency of a static library, which
# appears as $<LINK_ONLY:...>) and include dirs inside the root.
add_library(allowed_only STATIC lib.cpp)
target_link_libraries(allowed_only PRIVATE allowed::dep allowed_local)
target_link_options(allowed_only INTERFACE -fsanitize=address)
target_compile_options(allowed_only PRIVATE -O2 -Wshadow -fno-omit-frame-pointer)
target_include_directories(allowed_only PUBLIC
    "$<BUILD_INTERFACE:${_root}>" "$<INSTALL_INTERFACE:include>")
expect(allowed_only 0)

add_library(ros_public STATIC lib.cpp)
target_link_libraries(ros_public PUBLIC rclcpp::rclcpp)
expect(ros_public 2 "rclcpp::rclcpp")  # LINK_LIBRARIES + INTERFACE_LINK_LIBRARIES

add_library(ros_private_static STATIC lib.cpp)
target_link_libraries(ros_private_static PRIVATE rclcpp::rclcpp)
expect(ros_private_static 2 "rclcpp::rclcpp")  # LINK_LIBRARIES + $<LINK_ONLY:...>

add_library(escaped INTERFACE)
target_link_libraries(escaped INTERFACE harmless\#tag rclcpp::rclcpp)
expect(escaped 2 "harmless#tag")

add_library(ament STATIC lib.cpp)
fake_ament_target_dependencies(ament rclcpp)
expect(ament 5 "outside the allowed include roots")  # 2 links + 3 include properties

add_library(set_property_link INTERFACE)
set_property(TARGET set_property_link APPEND PROPERTY INTERFACE_LINK_LIBRARIES rclcpp::rclcpp)
expect(set_property_link 1 "rclcpp::rclcpp")

add_library(conditional_genex INTERFACE)
target_link_libraries(conditional_genex INTERFACE "$<$<BOOL:1>:rclcpp::rclcpp>")
expect(conditional_genex 1 "unsupported generator expression")

add_library(include_escape INTERFACE)
target_include_directories(include_escape INTERFACE "${_root}/../fake_ros/include")
expect(include_escape 1 "outside the allowed include roots")

# Usage requirements of an allowed target are followed transitively.
add_library(smuggler INTERFACE)
target_link_libraries(smuggler INTERFACE rclcpp::rclcpp)
target_include_directories(smuggler INTERFACE "${_ros_include}")
add_library(via_allowed STATIC lib.cpp)
target_link_libraries(via_allowed PRIVATE smuggler)
expect(via_allowed 2 "'smuggler' is not an allowed dependency")
add_library(allowed_but_tampered INTERFACE)
target_link_libraries(allowed_but_tampered INTERFACE allowed_local)
target_link_libraries(allowed_local INTERFACE rclcpp::rclcpp)  # tamper with the allowed target
expect(allowed_but_tampered 1 "allowed_local INTERFACE_LINK_LIBRARIES: 'rclcpp::rclcpp'")

add_library(link_option INTERFACE)
target_link_options(link_option INTERFACE "LINKER:--whole-archive,librclcpp.a,--no-whole-archive")
expect(link_option 1 "not an allowed link option")

add_library(link_directory INTERFACE)
target_link_directories(link_directory INTERFACE /opt/ros/jazzy/lib)
expect(link_directory 1 "link directories are not allowed")

add_library(include_flag STATIC lib.cpp)
target_compile_options(include_flag PRIVATE -isystem /opt/ros/jazzy/include)
target_compile_options(include_flag INTERFACE "-include" "rclcpp/rclcpp.hpp" -I/opt/ros/jazzy/include)
expect(include_flag 3 "can add external dependencies")

add_library(install_escape INTERFACE)
target_include_directories(install_escape INTERFACE "$<INSTALL_INTERFACE:../fake_ros/include>")
expect(install_escape 1 "install include '../fake_ros/include' is not allowed")

add_library(component OBJECT lib.cpp)
add_library(object_members STATIC lib.cpp $<TARGET_OBJECTS:component>)
expect(object_members 1 "unsupported generator expression '\\$<TARGET_OBJECTS:component>'")

add_library(foreign_source STATIC lib.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../outside.cpp")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../outside.cpp" PROPERTIES GENERATED TRUE)
expect(foreign_source 1 "outside the allowed source roots")

add_library(legacy_link_flags STATIC lib.cpp)
set_property(TARGET legacy_link_flags PROPERTY LINK_FLAGS "-lrclcpp")
expect(legacy_link_flags 1 "legacy flag properties are not allowed")

add_library(direct_link INTERFACE)
set_property(TARGET direct_link PROPERTY INTERFACE_LINK_LIBRARIES_DIRECT rclcpp::rclcpp)
expect(direct_link 1 "INTERFACE_LINK_LIBRARIES_DIRECT: 'rclcpp::rclcpp'")

add_library(legacy_link_interface STATIC lib.cpp)
set_property(TARGET legacy_link_interface PROPERTY LINK_INTERFACE_LIBRARIES rclcpp::rclcpp)
expect(legacy_link_interface 1 "LINK_INTERFACE_LIBRARIES: 'rclcpp::rclcpp'")

add_library(foreign_pch STATIC lib.cpp)
target_precompile_headers(foreign_pch PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../header_deps_e2e/fake_ros/include/rclcpp/a.hpp")
expect(foreign_pch 1 "foreign_pch PRECOMPILE_HEADERS: ")

# An allowed target that the check cannot see (an imported target local to a
# subdirectory) must fail closed instead of being treated as a plain name.
add_subdirectory(scoped)
expect(scoped_user 1 "allowed target 'scoped::dep' is not visible to the check")

# An allowed *plain* item that is in fact a target cannot be inspected.
add_library(plainlib INTERFACE)
target_link_libraries(plainlib INTERFACE rclcpp::rclcpp)
add_library(plain_is_target INTERFACE)
target_link_libraries(plain_is_target INTERFACE plainlib)
expect(plain_is_target 1 "'plainlib' is allowed only as a plain link item")

# A dependency added after the check is deferred must still be caught, which
# is how core/CMakeLists.txt uses the check.
add_library(late INTERFACE)
cmake_language(DEFER CALL expect late 1 "rclcpp::rclcpp")
target_link_libraries(late INTERFACE rclcpp::rclcpp)
