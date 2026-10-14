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
    src/common/logging.cpp
    src/common/rng.cpp)
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
