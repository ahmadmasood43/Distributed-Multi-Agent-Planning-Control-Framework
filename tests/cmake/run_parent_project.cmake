# Configures tests/cmake/fixtures/parent_project with CASE (which must succeed),
# then checks mapc_core's generated requirements in that build.
#
#   cmake -DMAPC_SOURCE_DIR=<repo> -DWORK_DIR=<scratch> -DCASE=<case>
#         [-DEXPECT=<regex>] -DGENERATOR=<gen> [-DMAKE_PROGRAM=<prog>]
#         -DCXX=<compiler> -P tests/cmake/run_parent_project.cmake
#
# Without EXPECT the generated requirements must pass; with EXPECT they must be
# rejected with matching output.

cmake_minimum_required(VERSION 3.21)

file(REMOVE_RECURSE "${WORK_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${MAPC_SOURCE_DIR}/tests/cmake/fixtures/parent_project"
            -B "${WORK_DIR}" -G "${GENERATOR}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
            "-DCMAKE_CXX_COMPILER=${CXX}" -DCMAKE_BUILD_TYPE=Debug
            "-DMAPC_SOURCE_DIR=${MAPC_SOURCE_DIR}" "-DCASE=${CASE}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Configuring the parent project (CASE=${CASE}) failed:\n${_out}${_err}")
endif()

file(GLOB_RECURSE _dumps "${WORK_DIR}/mapc_core-Debug.requirements.cmake")
list(LENGTH _dumps _n)
if(NOT _n EQUAL 1)
    message(FATAL_ERROR "Expected one generated requirements file, found ${_n}: ${_dumps}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DREQUIREMENTS=${_dumps}"
            -P "${MAPC_SOURCE_DIR}/tools/check_generated_requirements.cmake"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
set(_output "${_out}${_err}")

if(NOT DEFINED EXPECT OR EXPECT STREQUAL "")
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "CASE=${CASE}: generated requirements were rejected:\n${_output}")
    endif()
elseif(_rc EQUAL 0 OR NOT _output MATCHES "${EXPECT}")
    message(FATAL_ERROR "CASE=${CASE}: expected a rejection matching '${EXPECT}':\n${_output}")
endif()
message(STATUS "CASE=${CASE}: as expected")
