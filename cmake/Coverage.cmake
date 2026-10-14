# Adds coverage instrumentation globally when MAPC_ENABLE_COVERAGE is ON.
# Reports are generated outside CMake (gcovr in CI), keeping this module small.

if(NOT MAPC_ENABLE_COVERAGE)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "MAPC_ENABLE_COVERAGE requires GCC or Clang")
endif()

if(MAPC_SANITIZER)
    message(FATAL_ERROR "MAPC_ENABLE_COVERAGE cannot be combined with MAPC_SANITIZER")
endif()

message(STATUS "mapc: enabling coverage instrumentation")
# Atomic counter updates keep hit counts exact in multi-threaded tests; without
# them gcov can report negative or torn counts.
add_compile_options(--coverage -O0 -g -fprofile-update=atomic)
add_link_options(--coverage)
list(APPEND MAPC_INSTRUMENTATION_LINK_OPTIONS --coverage)  # exported by mapc_core
