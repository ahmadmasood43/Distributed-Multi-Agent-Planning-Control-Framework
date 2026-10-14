# Defines the INTERFACE target mapc::warnings carrying the project warning set.
# Only first-party targets link it, so third-party code is never built with
# -Werror.

add_library(mapc_warnings INTERFACE)
add_library(mapc::warnings ALIAS mapc_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mapc_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wunused)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(mapc_warnings INTERFACE
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op)
    endif()

    if(MAPC_WARNINGS_AS_ERRORS)
        target_compile_options(mapc_warnings INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(mapc_warnings INTERFACE /W4 /permissive-)
    if(MAPC_WARNINGS_AS_ERRORS)
        target_compile_options(mapc_warnings INTERFACE /WX)
    endif()
endif()
