# Runs a CMake invocation that is expected to fail and checks *how* it fails.
#
#   cmake [-DSCRIPT=<script.cmake>] -DSCRIPT_ARGS=<arg1|arg2|...> -DEXPECT=<regex>
#         [-DEXPECT_ALSO=<regex>] -P tests/cmake/expect_failure.cmake
#
# SCRIPT_ARGS is a '|'-separated argument list for `cmake`. With SCRIPT, it
# holds -D definitions and the script is run with -P; without SCRIPT, it is the
# complete command line (e.g. -S <src>|-B <build> to configure a project).
# Passes only if the script exits with a non-zero status AND its output matches
# EXPECT (and EXPECT_ALSO, when given). A plain PASS_REGULAR_EXPRESSION would
# ignore the exit status.

cmake_minimum_required(VERSION 3.21)

foreach(_var EXPECT)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "expect_failure.cmake: ${_var} is required")
    endif()
endforeach()

string(REPLACE "|" ";" _args "${SCRIPT_ARGS}")
if(DEFINED SCRIPT)
    list(APPEND _args -P "${SCRIPT}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_args}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)

set(_output "${_out}${_err}")

if(_result EQUAL 0)
    message(FATAL_ERROR "Expected 'cmake ${_args}' to fail, but it succeeded:\n${_output}")
endif()

foreach(_regex IN ITEMS "${EXPECT}" "${EXPECT_ALSO}")
    if(NOT _regex STREQUAL "" AND NOT _output MATCHES "${_regex}")
        message(FATAL_ERROR "Output of 'cmake ${_args}' does not match '${_regex}':\n${_output}")
    endif()
endforeach()

message(STATUS "Failed as expected (exit ${_result})")
