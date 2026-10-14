cmake_minimum_required(VERSION 3.21)

# CMake >= 3.24: give files extracted by FetchContent the extraction time as
# their timestamp, so updating a pinned dependency always triggers a rebuild.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(mapc
    VERSION 0.1.0
    DESCRIPTION "Distributed multi-agent planning and control framework"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
include(CMakeDependentOption)

option(MAPC_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(MAPC_BUILD_BENCHMARKS "Build the micro-benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAPC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MAPC_ENABLE_COVERAGE "Instrument the build for gcov/llvm-cov coverage" OFF)
option(MAPC_ENABLE_CLANG_TIDY "Run clang-tidy on mapc targets while compiling" OFF)
option(MAPC_INSTALL "Generate install and package-config rules" ${PROJECT_IS_TOP_LEVEL})
set(MAPC_SANITIZER "" CACHE STRING
    "Sanitizers to enable: empty, 'address', 'undefined', 'address;undefined' or 'thread'")

# ---------------------------------------------------------------------------
# Global toolchain settings
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endif()

# Sanitizer and coverage flags are applied globally (before dependencies are
# fetched) so third-party code such as GoogleTest is instrumented consistently.
include(GNUInstallDirs)
include(Sanitizers)
include(Coverage)
include(CompilerWarnings)
include(ClangTidy)
include(ArchitectureRules)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
add_subdirectory(core)

if(MAPC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MAPC_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(MAPC_INSTALL)
    include(Install)
endif()
