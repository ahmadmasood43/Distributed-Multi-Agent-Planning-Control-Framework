find_package(Threads REQUIRED)

# The dependency check below runs in this project's top directory, which cannot
# see an imported target that is local to this directory. Promote
# Threads::Threads if it was created here (a parent project may already
# provide it, visibly, from an enclosing directory).
get_directory_property(_mapc_imported_here IMPORTED_TARGETS)
if("Threads::Threads" IN_LIST _mapc_imported_here)
    get_target_property(_mapc_threads_global Threads::Threads IMPORTED_GLOBAL)
    if(NOT _mapc_threads_global)
        set_target_properties(Threads::Threads PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/include/mapc/version.hpp"
    @ONLY)

add_library(mapc_core
    src/allocation/cost_model.cpp
    src/allocation/distance_oracle.cpp
    src/allocation/hungarian.cpp
    src/allocation/task_allocator.cpp
    src/behavior/blackboard.cpp
    src/behavior/composites.cpp
    src/behavior/decorators.cpp
    src/behavior/leaves.cpp
    src/behavior/node.cpp
    src/behavior/tree.cpp
    src/common/logging.cpp
    src/common/rng.cpp
    src/graph/graph.cpp
    src/graph/graph_overlay.cpp
    src/graph/grid_map.cpp
    src/planning/best_first_search.cpp
    src/planning/heuristics.cpp
    src/planning/planner.cpp
    src/world/task.cpp)
add_library(mapc::core ALIAS mapc_core)

target_include_directories(mapc_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_compile_features(mapc_core PUBLIC cxx_std_17)

# Sanitizer/coverage builds: consumers of the (installed) library must link the
# matching runtime, so the link flags are part of its usage requirements.
if(MAPC_INSTRUMENTATION_LINK_OPTIONS)
    target_link_options(mapc_core INTERFACE ${MAPC_INSTRUMENTATION_LINK_OPTIONS})
endif()
target_link_libraries(mapc_core
    PUBLIC Threads::Threads
    PRIVATE $<BUILD_INTERFACE:mapc::warnings>)

set_target_properties(mapc_core PROPERTIES
    OUTPUT_NAME mapc_core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    EXPORT_NAME core)

mapc_enable_clang_tidy(mapc_core)

# The core library must stay free of middleware (ROS) and other third-party
# dependencies. The check runs at the end of this project's top directory, so
# dependencies added to mapc_core (or to the targets it links) anywhere in this
# repository are caught. It also dumps the requirements as generated, which
# architecture.generated_requirements_* re-checks after everything (including
# later deferred calls) has run.
mapc_defer_dependency_check(mapc_core
    ALLOWED_TARGETS Threads::Threads mapc::warnings mapc_warnings
    ALLOWED_LINK_ITEMS -pthread pthread -lpthread
    ALLOWED_LINK_OPTIONS ${MAPC_INSTRUMENTATION_LINK_OPTIONS}
    ALLOWED_INCLUDE_ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}/generated/include"
    ALLOWED_INSTALL_INCLUDES "${CMAKE_INSTALL_INCLUDEDIR}"
    ALLOWED_SOURCE_ROOTS "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}"
    DUMP_FILE "${PROJECT_BINARY_DIR}/architecture/mapc_core-$<CONFIG>.requirements.cmake")

# ---------------------------------------------------------------------------
# mapc_io: file formats that need a third-party parser (YAML). Kept out of
# mapc_core so the core library stays free of external dependencies. Not
# installed yet (see docs/FOLLOWUPS.md).
# ---------------------------------------------------------------------------
include(FetchContent)

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# yaml-cpp 0.8.0 declares cmake_minimum_required(VERSION 3.4); CMake >= 4.0
# refuses that unless a policy minimum is supplied.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
    URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16)
FetchContent_MakeAvailable(yaml-cpp)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# Treat yaml-cpp's headers as system headers so our warning flags (and
# -Werror) do not apply to third-party code.
get_target_property(_mapc_yaml_includes yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(yaml-cpp PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_mapc_yaml_includes}")

add_library(mapc_io
    src/io/graph_yaml.cpp)
add_library(mapc::io ALIAS mapc_io)
target_link_libraries(mapc_io
    PUBLIC mapc::core
    PRIVATE yaml-cpp::yaml-cpp $<BUILD_INTERFACE:mapc::warnings>)
set_target_properties(mapc_io PROPERTIES
    OUTPUT_NAME mapc_io
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
mapc_enable_clang_tidy(mapc_io)

# mapc_io may add yaml-cpp, and nothing else, on top of mapc_core.
mapc_defer_dependency_check(mapc_io
    ALLOWED_TARGETS mapc_core mapc::core yaml-cpp yaml-cpp::yaml-cpp
                    Threads::Threads mapc::warnings mapc_warnings
    ALLOWED_LINK_ITEMS -pthread pthread -lpthread
    ALLOWED_LINK_OPTIONS ${MAPC_INSTRUMENTATION_LINK_OPTIONS}
    ALLOWED_INCLUDE_ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}/generated/include"
        "${yaml-cpp_SOURCE_DIR}/include"
    ALLOWED_INSTALL_INCLUDES "${CMAKE_INSTALL_INCLUDEDIR}"
    ALLOWED_SOURCE_ROOTS "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}"
    DUMP_FILE "${PROJECT_BINARY_DIR}/architecture/mapc_io-$<CONFIG>.requirements.cmake")
