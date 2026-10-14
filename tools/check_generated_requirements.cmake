# Applies the dependency rules of cmake/ArchitectureRules.cmake to the target
# requirements *as generated*.
#
#   cmake -DREQUIREMENTS=<dump file> -P tools/check_generated_requirements.cmake
#
# The dump is written by file(GENERATE) after all configure-time code has run,
# including callbacks deferred after the configure-time check, so a dependency
# added "after the check" is still visible here.

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED REQUIREMENTS OR NOT EXISTS "${REQUIREMENTS}")
    message(FATAL_ERROR "REQUIREMENTS must name a generated requirements file (got '${REQUIREMENTS}')")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ArchitectureRules.cmake")
include("${REQUIREMENTS}")

if(NOT DEFINED MAPC_DUMP_ROOT)
    message(FATAL_ERROR "${REQUIREMENTS} is not a requirements dump")
endif()

set(MAPC_DUMP_MODE TRUE)
_mapc_evaluate_rules("${MAPC_DUMP_ROOT}" _violations _visited ${MAPC_DUMP_RULE_ARGS})

if(_violations)
    list(JOIN _violations "\n  " _report)
    message(FATAL_ERROR
        "Generated requirements of '${MAPC_DUMP_ROOT}' violate the dependency allowlist:\n  ${_report}")
endif()

list(LENGTH _visited _n)
message(STATUS "Generated requirements of '${MAPC_DUMP_ROOT}' (${_n} target(s)) satisfy the allowlist")
