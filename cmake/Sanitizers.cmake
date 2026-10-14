# Applies -fsanitize flags globally based on MAPC_SANITIZER.
#
# Accepted values: "" (none), "address", "undefined", "address;undefined",
# "thread". ThreadSanitizer cannot be combined with AddressSanitizer, so the
# combination is rejected at configure time instead of failing at link time.

if(NOT MAPC_SANITIZER)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "MAPC_SANITIZER is only supported with GCC or Clang "
                        "(current compiler: ${CMAKE_CXX_COMPILER_ID})")
endif()

set(_mapc_known_sanitizers address undefined thread)
set(_mapc_sanitizers ${MAPC_SANITIZER})
list(REMOVE_DUPLICATES _mapc_sanitizers)

foreach(_san IN LISTS _mapc_sanitizers)
    if(NOT _san IN_LIST _mapc_known_sanitizers)
        message(FATAL_ERROR "Unknown sanitizer '${_san}' in MAPC_SANITIZER. "
                            "Expected a subset of: ${_mapc_known_sanitizers}")
    endif()
endforeach()

if("thread" IN_LIST _mapc_sanitizers AND "address" IN_LIST _mapc_sanitizers)
    message(FATAL_ERROR "MAPC_SANITIZER: 'thread' and 'address' cannot be combined")
endif()

list(JOIN _mapc_sanitizers "," _mapc_sanitize_arg)
message(STATUS "mapc: enabling sanitizers: ${_mapc_sanitize_arg}")

add_compile_options(
    -fsanitize=${_mapc_sanitize_arg}
    -fno-omit-frame-pointer
    -fno-sanitize-recover=all)
add_link_options(-fsanitize=${_mapc_sanitize_arg})
# Instrumented objects reference the sanitizer runtime, so anything linking the
# installed library needs the flag too; mapc_core exports it (see core/).
list(APPEND MAPC_INSTRUMENTATION_LINK_OPTIONS -fsanitize=${_mapc_sanitize_arg})

if("undefined" IN_LIST _mapc_sanitizers AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang does not include float-divide-by-zero in -fsanitize=undefined.
    add_compile_options(-fsanitize=float-divide-by-zero)
endif()

unset(_san)
unset(_mapc_known_sanitizers)
unset(_mapc_sanitizers)
unset(_mapc_sanitize_arg)
