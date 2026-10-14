# Configures tests/cmake/fixtures/generated_requirements with CASE, then runs
# tools/check_generated_requirements.cmake on the generated dump.
#
#   cmake -DMAPC_SOURCE_DIR=<repo> -DWORK_DIR=<scratch> -DCASE=<case>
#         [-DEXPECT=<regex>] -DGENERATOR=<gen> [-DMAKE_PROGRAM=<prog>]
#         -DCXX=<compiler> -P tests/cmake/run_generated_requirements.cmake
#
# Without EXPECT the check must pass; with EXPECT it must fail with matching
# output. Configuration itself must succeed in every case: the point is that
# the mutation happens after the configure-time check.

cmake_minimum_required(VERSION 3.21)

file(REMOVE_RECURSE "${WORK_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${MAPC_SOURCE_DIR}/tests/cmake/fixtures/generated_requirements"
            -B "${WORK_DIR}" -G "${GENERATOR}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
            "-DCMAKE_CXX_COMPILER=${CXX}" "-DMAPC_SOURCE_DIR=${MAPC_SOURCE_DIR}" "-DCASE=${CASE}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Configuring the fixture (CASE=${CASE}) failed:\n${_out}${_err}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DREQUIREMENTS=${WORK_DIR}/core.requirements.cmake"
            -P "${MAPC_SOURCE_DIR}/tools/check_generated_requirements.cmake"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
set(_output "${_out}${_err}")

if(NOT DEFINED EXPECT OR EXPECT STREQUAL "")
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "CASE=${CASE}: the generated requirements were rejected:\n${_output}")
    endif()
else()
    if(_rc EQUAL 0)
        message(FATAL_ERROR "CASE=${CASE}: the generated requirements were accepted:\n${_output}")
    endif()
    if(NOT _output MATCHES "${EXPECT}")
        message(FATAL_ERROR "CASE=${CASE}: output does not match '${EXPECT}':\n${_output}")
    endif()
endif()
message(STATUS "CASE=${CASE}: as expected")
