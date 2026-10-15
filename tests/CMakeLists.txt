include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
add_executable(mapc_unit_tests
    unit/common/strong_id_test.cpp
    unit/common/time_test.cpp
    unit/common/uint128_test.cpp
    unit/common/result_test.cpp
    unit/common/rng_test.cpp
    unit/common/logging_test.cpp
    unit/graph/graph_test.cpp
    unit/graph/grid_map_test.cpp
    unit/io/graph_yaml_test.cpp
    unit/planning/best_first_search_test.cpp
    unit/planning/planner_properties_test.cpp
    unit/version_test.cpp)
if(MAPC_BUILD_APPS)
    target_sources(mapc_unit_tests PRIVATE unit/apps/plan_cli_test.cpp)
    target_link_libraries(mapc_unit_tests PRIVATE mapc_plan_cli)
endif()
target_compile_definitions(mapc_unit_tests PRIVATE
    MAPC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

target_link_libraries(mapc_unit_tests PRIVATE
    mapc::core
    mapc::io
    mapc::warnings
    GTest::gtest_main
    GTest::gmock)

mapc_enable_clang_tidy(mapc_unit_tests)

gtest_discover_tests(mapc_unit_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)

# ---------------------------------------------------------------------------
# Every public header must compile on its own (include what you use).
# One translation unit is generated per header and compiled into an object
# library; a missing include fails the build.
# ---------------------------------------------------------------------------
# Public headers from the source tree and from the generated include directory
# (headers generated at configure time, such as mapc/version.hpp).
file(GLOB_RECURSE MAPC_PUBLIC_HEADERS CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/core/include"
    "${PROJECT_SOURCE_DIR}/core/include/mapc/*.hpp")
file(GLOB_RECURSE _mapc_generated_headers
    RELATIVE "${PROJECT_BINARY_DIR}/generated/include"
    "${PROJECT_BINARY_DIR}/generated/include/mapc/*.hpp")
list(APPEND MAPC_PUBLIC_HEADERS ${_mapc_generated_headers})
list(REMOVE_DUPLICATES MAPC_PUBLIC_HEADERS)

set(_header_check_sources "")
foreach(_header IN LISTS MAPC_PUBLIC_HEADERS)
    string(MAKE_C_IDENTIFIER "${_header}" _id)
    set(_tu "${CMAKE_CURRENT_BINARY_DIR}/header_check/${_id}.cpp")
    file(CONFIGURE OUTPUT "${_tu}" CONTENT "#include <${_header}>\n#include <${_header}>  // include guard check\n")
    list(APPEND _header_check_sources "${_tu}")
endforeach()

add_library(mapc_header_check OBJECT ${_header_check_sources})
target_link_libraries(mapc_header_check PRIVATE mapc::core mapc::warnings)

# ---------------------------------------------------------------------------
# Architecture rule: core/ must never depend on ROS. Four layers:
#  1. tools/check_no_ros_in_core.cmake   source-text scan (fast, readable)
#  2. cmake/ArchitectureRules.cmake      what CMake resolved for mapc_core
#                                        (enforced at configure time)
#  3. tools/check_header_deps.cmake      headers the compiler actually read
#  4. tools/check_core_symbols.cmake     symbols the built library needs
# Each checker is itself tested: rejections must exit non-zero AND print the
# expected diagnostic; clean inputs must pass.
# ---------------------------------------------------------------------------
set(_ros_checker "${PROJECT_SOURCE_DIR}/tools/check_no_ros_in_core.cmake")
set(_deps_checker "${PROJECT_SOURCE_DIR}/tools/check_header_deps.cmake")
set(_expect_failure "${CMAKE_CURRENT_SOURCE_DIR}/cmake/expect_failure.cmake")
set(_fixtures "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fixtures")

# mapc_add_rejection_test(<name> <script or ""> <'|'-separated args> <regex> [<regex>])
function(mapc_add_rejection_test name script script_args regex)
    set(_also "")
    if(ARGC GREATER 4)
        set(_also "${ARGV4}")
    endif()
    set(_script_arg "")
    if(NOT script STREQUAL "")
        set(_script_arg "-DSCRIPT=${script}")
    endif()
    add_test(NAME architecture.${name}
        COMMAND ${CMAKE_COMMAND}
            ${_script_arg}
            "-DSCRIPT_ARGS=${script_args}"
            "-DEXPECT=${regex}"
            "-DEXPECT_ALSO=${_also}"
            -P ${_expect_failure})
    set_tests_properties(architecture.${name} PROPERTIES LABELS architecture)
endfunction()

function(mapc_add_ros_rejection_test name fixture)
    mapc_add_rejection_test(${name} "${_ros_checker}" "-DSCAN_DIR=${_fixtures}/${fixture}" ${ARGN})
endfunction()

# --- Layer 1: source-text scan ----------------------------------------------
add_test(NAME architecture.core_has_no_ros_dependency
    COMMAND ${CMAKE_COMMAND} -DSCAN_DIR=${PROJECT_SOURCE_DIR}/core -P ${_ros_checker})
# Generated public headers exist only in the build tree; scan them after
# generation, and scan the installed header set, which is what clients get.
add_test(NAME architecture.generated_headers_have_no_ros_dependency
    COMMAND ${CMAKE_COMMAND} -DSCAN_DIR=${PROJECT_BINARY_DIR}/generated -DALL_FILES=ON -P ${_ros_checker})
set_tests_properties(architecture.generated_headers_have_no_ros_dependency PROPERTIES LABELS architecture)
if(MAPC_INSTALL)
    add_test(NAME architecture.installed_headers_have_no_ros_dependency
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${PROJECT_BINARY_DIR}
            -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/fixtures/installed_headers
            -DMAPC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_installed_scan.cmake)
    set_tests_properties(architecture.installed_headers_have_no_ros_dependency PROPERTIES LABELS architecture)
endif()

add_test(NAME architecture.ros_checker_accepts_clean_tree
    COMMAND ${CMAKE_COMMAND} -DSCAN_DIR=${_fixtures}/clean -P ${_ros_checker})
set_tests_properties(
    architecture.core_has_no_ros_dependency
    architecture.ros_checker_accepts_clean_tree
    PROPERTIES LABELS architecture)

mapc_add_ros_rejection_test(ros_checker_detects_include ros_include
    "node\\.hpp:7: #include <rclcpp/rclcpp\\.hpp>"
    "node\\.hpp:8: #include \"geometry_msgs/msg/pose\\.hpp\"")
mapc_add_ros_rejection_test(ros_checker_detects_include_between_comments ros_include_hidden
    "loader\\.cpp:4: #include <rclcpp/rclcpp\\.hpp>"
    "loader\\.cpp:6: #include <std_msgs/msg/string\\.hpp>.*loader\\.cpp:8: #include <rcutils/logging\\.h>.*loader\\.cpp:10: #include <rmw/rmw\\.h>")
mapc_add_ros_rejection_test(ros_checker_detects_obfuscated_includes ros_include_tricky
    "tricky\\.cpp:4: #include <rclcpp/a\\.hpp>.*tricky\\.cpp:5: #include <rclcpp/b\\.hpp>.*tricky\\.cpp:6: #include <rclcpp/c\\.hpp>.*tricky\\.cpp:8: #include <rclcpp/d\\.hpp>"
    "tricky\\.cpp:10: #include <rclcpp/e\\.hpp>.*tricky\\.cpp:11: #include <rclcpp/f\\.hpp>.*tricky\\.cpp:13: #include <rclcpp/g\\.hpp>.*tricky\\.cpp:16: #include <rclcpp/h\\.hpp>")
mapc_add_ros_rejection_test(ros_checker_detects_ros_identifiers ros_identifiers
    "api\\.hpp:5: uses ROS identifier 'rcl_init'.*api\\.hpp:6: uses ROS identifier 'g_rcutils_logging_initialized'.*api\\.hpp:8: uses ROS identifier 'namespace std_msgs'"
    "api\\.hpp:13: uses ROS identifier 'rcl_init'.*api\\.hpp:18: uses ROS identifier 'std_msgs__msg__String__init'.*api\\.hpp:21: uses ROS identifier 'rclcpp::'")
mapc_add_ros_rejection_test(ros_checker_detects_find_package ros_cmake
    "CMakeLists\\.txt: find_package\\(rclcpp ")
mapc_add_ros_rejection_test(ros_checker_detects_multiline_find_package ros_cmake_multiline
    "CMakeLists\\.txt: find_package\\( rclcpp ")
mapc_add_ros_rejection_test(ros_checker_detects_link_dependency ros_link
    "CMakeLists\\.txt: target_link_libraries\\(fixture .*rclcpp::rclcpp")
mapc_add_ros_rejection_test(ros_checker_detects_case_insensitive_commands ros_cmake_case
    "CMakeLists\\.txt: find_package\\(rclcpp "
    "CMakeLists\\.txt: find_package\\(\"std_msgs\"")
mapc_add_ros_rejection_test(ros_checker_detects_obfuscated_cmake_dependencies ros_cmake_tricky
    "find_package\\(\"geometry_msgs\".*ament_target_dependencies\\(core system std_msgs\\)"
    "harmless__tag rclcpp::rclcpp.*harmless__tag rmw::rmw.*\"optional name \" rcutils::rcutils")
mapc_add_ros_rejection_test(ros_checker_rejects_missing_dir does_not_exist
    "SCAN_DIR must name an existing directory")

mapc_add_rejection_test(installed_header_scan_detects_generated_header
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_installed_scan.cmake"
    "-DFIXTURE=${_fixtures}/installed_headers|-DGENERATOR=${CMAKE_GENERATOR}|-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures/installed_headers_build|-DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/fixtures/installed_headers_prefix|-DMAPC_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "include/mapc/generated_probe\\.hpp:2: uses ROS identifier 'rcl_init'"
    "include/mapc/generated_probe:2: uses ROS identifier 'rcl_init'")
# The same, with a non-default (absolute) include destination: the scan must
# follow the configured destination, staged through DESTDIR.
mapc_add_rejection_test(installed_header_scan_follows_include_destination
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_installed_scan.cmake"
    "-DFIXTURE=${_fixtures}/installed_headers|-DGENERATOR=${CMAKE_GENERATOR}|-DFIXTURE_ARGS=-DCMAKE_INSTALL_INCLUDEDIR=/opt/custom/mapc_headers|-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures/installed_headers_abs_build|-DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/fixtures/installed_headers_abs_prefix|-DMAPC_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "Installed files use ROS"
    "stage/opt/custom/mapc_headers/mapc/generated_probe:2: uses ROS identifier 'rcl_init'")

# --- Layer 2: configure-time allowlist ----------------------------------------
# The fixture project checks itself and fails to configure on any mismatch.
add_test(NAME architecture.dependency_allowlist
    COMMAND ${CMAKE_COMMAND}
        -S ${_fixtures}/dependency_rules
        -B ${CMAKE_CURRENT_BINARY_DIR}/fixtures/dependency_rules
        -G ${CMAKE_GENERATOR}
        -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DMAPC_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_tests_properties(architecture.dependency_allowlist PROPERTIES
    LABELS architecture
    FAIL_REGULAR_EXPRESSION "FAIL ")

# The same rules applied to the requirements as generated, which also covers
# changes made after the configure-time check (e.g. later deferred calls).
add_test(NAME architecture.generated_requirements_are_allowlisted
    COMMAND ${CMAKE_COMMAND}
        "-DREQUIREMENTS=${PROJECT_BINARY_DIR}/architecture/mapc_core-$<CONFIG>.requirements.cmake"
        -P ${PROJECT_SOURCE_DIR}/tools/check_generated_requirements.cmake)
set_tests_properties(architecture.generated_requirements_are_allowlisted PROPERTIES LABELS architecture)
add_test(NAME architecture.generated_io_requirements_are_allowlisted
    COMMAND ${CMAKE_COMMAND}
        "-DREQUIREMENTS=${PROJECT_BINARY_DIR}/architecture/mapc_io-$<CONFIG>.requirements.cmake"
        -P ${PROJECT_SOURCE_DIR}/tools/check_generated_requirements.cmake)
set_tests_properties(architecture.generated_io_requirements_are_allowlisted PROPERTIES LABELS architecture)

# mapc_add_generated_requirements_test(<case> [<expected violation regex>])
function(mapc_add_generated_requirements_test case)
    set(_expect "")
    if(ARGC GREATER 1)
        set(_expect "${ARGV1}")
    endif()
    add_test(NAME architecture.generated_requirements_${case}
        COMMAND ${CMAKE_COMMAND}
            -DMAPC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures/generated_requirements_${case}
            -DCASE=${case}
            "-DEXPECT=${_expect}"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DCXX=${CMAKE_CXX_COMPILER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_generated_requirements.cmake)
    set_tests_properties(architecture.generated_requirements_${case} PROPERTIES LABELS architecture)
endfunction()
mapc_add_generated_requirements_test(clean)
mapc_add_generated_requirements_test(deferred_link
    "core LINK_LIBRARIES: 'rclcpp::rclcpp' is not an allowed dependency")
mapc_add_generated_requirements_test(deferred_via_allowed
    "warnings INTERFACE_LINK_LIBRARIES: 'rclcpp::rclcpp' is not an allowed dependency")
mapc_add_generated_requirements_test(deferred_link_flags
    "core LINK_FLAGS: legacy flag properties are not allowed")
mapc_add_generated_requirements_test(late_plain_target
    "core LINK_LIBRARIES: 'pthread' is allowed only as a plain link item, but a target with that name exists")
mapc_add_generated_requirements_test(deferred_direct_link
    "core INTERFACE_LINK_LIBRARIES_DIRECT: 'rclcpp::rclcpp' is not an allowed dependency")

# A parent project using mapc via add_subdirectory must configure cleanly, also
# when an enclosing directory already found Threads. A parent that adds a ROS
# dependency to mapc_core afterwards is outside the guarantee (it controls its
# own build), but the change must be visible in the generated requirements.
# Reuse the yaml-cpp sources this build already downloaded, so the fixture
# projects configure offline and without repeated downloads.
FetchContent_GetProperties(yaml-cpp SOURCE_DIR _mapc_yaml_source_dir)

function(mapc_add_parent_project_test case)
    set(_expect "")
    if(ARGC GREATER 1)
        set(_expect "${ARGV1}")
    endif()
    add_test(NAME architecture.parent_project_${case}
        COMMAND ${CMAKE_COMMAND}
            -DMAPC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures/parent_project_${case}
            -DCASE=${case}
            "-DEXPECT=${_expect}"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DYAML_SOURCE_DIR=${_mapc_yaml_source_dir}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_parent_project.cmake)
    set_tests_properties(architecture.parent_project_${case} PROPERTIES LABELS architecture)
endfunction()
mapc_add_parent_project_test(clean)
mapc_add_parent_project_test(intermediate_threads)
mapc_add_parent_project_test(late_link
    "mapc_core LINK_LIBRARIES: 'rclcpp::rclcpp' is not an allowed dependency")

# --- Layer 3: headers the compiler actually read ------------------------------
add_test(NAME architecture.header_deps_accepts_clean_record
    COMMAND ${CMAKE_COMMAND} -DDEPS_FILE=${_fixtures}/header_deps/deps_clean.txt -P ${_deps_checker})
set_tests_properties(architecture.header_deps_accepts_clean_record PROPERTIES LABELS architecture)
mapc_add_rejection_test(header_deps_detects_ros_headers "${_deps_checker}"
    "-DDEPS_FILE=${_fixtures}/header_deps/deps_ros.txt"
    "mapc_core\\.dir/src/common/logging\\.cpp\\.o: /opt/ros/jazzy/include/rclcpp/"
    "mapc_header_check\\.dir/.*: /home/dev/ros2_ws/install/std_msgs/")
mapc_add_rejection_test(header_deps_rejects_stale_record "${_deps_checker}"
    "-DDEPS_FILE=${_fixtures}/header_deps/deps_stale.txt" "is STALE; rebuild first")
mapc_add_rejection_test(header_deps_rejects_missing_record "${_deps_checker}"
    "-DDEPS_FILE=${_fixtures}/header_deps/deps_empty.txt" "No dependency information found")

if(CMAKE_GENERATOR MATCHES "Ninja")
    # Runs against this build tree; ctest runs after the build, so the record
    # covers every core object and header-check object.
    add_test(NAME architecture.core_headers_are_ros_free
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DNINJA=${CMAKE_MAKE_PROGRAM}
                "-DOWNED_ROOTS=${PROJECT_SOURCE_DIR}/core|${PROJECT_BINARY_DIR}/generated"
                -P ${_deps_checker})
    add_test(NAME architecture.compiled_checks_detect_real_dependencies
        COMMAND ${CMAKE_COMMAND}
            -DMAPC_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/fixtures/header_deps_e2e
            -DNINJA=${CMAKE_MAKE_PROGRAM}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DNM=${CMAKE_NM}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_header_deps_e2e.cmake)
    set_tests_properties(
        architecture.core_headers_are_ros_free
        architecture.compiled_checks_detect_real_dependencies
        PROPERTIES LABELS architecture)
endif()

# --- Layer 4: symbols the built library needs ---------------------------------
if(CMAKE_NM)
    add_test(NAME architecture.core_symbols_are_ros_free
        COMMAND ${CMAKE_COMMAND} -DLIBRARY=$<TARGET_FILE:mapc_core> -DNM=${CMAKE_NM}
                -P ${PROJECT_SOURCE_DIR}/tools/check_core_symbols.cmake)
    add_test(NAME architecture.io_symbols_are_ros_free
        COMMAND ${CMAKE_COMMAND} -DLIBRARY=$<TARGET_FILE:mapc_io> -DNM=${CMAKE_NM}
                -DALLOWED_NEEDED=libmapc_core
                -P ${PROJECT_SOURCE_DIR}/tools/check_core_symbols.cmake)
    set_tests_properties(
        architecture.core_symbols_are_ros_free
        architecture.io_symbols_are_ros_free
        PROPERTIES LABELS architecture)
endif()
