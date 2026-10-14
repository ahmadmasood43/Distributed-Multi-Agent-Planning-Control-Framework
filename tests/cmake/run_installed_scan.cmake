# Installs a build tree into a scratch staging area and scans everything that
# was installed: exactly what a find_package client receives, wherever a file
# was generated or copied from, whatever its name, and whatever include
# destination was configured (installation goes through DESTDIR, so absolute
# destinations are staged inside the scratch area too).
#
#   cmake -DBUILD_DIR=<build> -DPREFIX=<scratch> -DMAPC_SOURCE_DIR=<repo>
#         [-DFIXTURE=<project dir> -DGENERATOR=<gen> [-DFIXTURE_ARGS=<a|b>]]
#         -P tests/cmake/run_installed_scan.cmake
#
# With FIXTURE, that project is first configured into BUILD_DIR.

cmake_minimum_required(VERSION 3.21)

if(DEFINED FIXTURE)
    file(REMOVE_RECURSE "${BUILD_DIR}")
    string(REPLACE "|" ";" _fixture_args "${FIXTURE_ARGS}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE}" -B "${BUILD_DIR}" -G "${GENERATOR}" ${_fixture_args}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Configuring ${FIXTURE} failed:\n${_out}${_err}")
    endif()
endif()

set(_stage "${PREFIX}/stage")
file(REMOVE_RECURSE "${PREFIX}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR=${_stage}"
            "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}/root"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Installing ${BUILD_DIR} failed:\n${_out}${_err}")
endif()

file(GLOB_RECURSE _installed LIST_DIRECTORIES false "${_stage}/*")
list(LENGTH _installed _n)
if(_n EQUAL 0)
    message(FATAL_ERROR "Nothing was installed from ${BUILD_DIR}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DSCAN_DIR=${_stage}" -DALL_FILES=ON
            -P "${MAPC_SOURCE_DIR}/tools/check_no_ros_in_core.cmake"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Installed files use ROS:\n${_out}${_err}")
endif()
message(STATUS "Installed ${_n} file(s); ${_out}")
