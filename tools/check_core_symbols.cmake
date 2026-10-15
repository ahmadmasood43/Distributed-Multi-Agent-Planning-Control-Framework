# Fails if the built core library references ROS symbols.
#
#   cmake -DLIBRARY=<path to libmapc_core.a/.so> [-DNM=<nm>] [-DREADELF=<readelf>]
#         [-DALLOWED_NEEDED=<lib1|lib2>] -P tools/check_core_symbols.cmake
#
# ALLOWED_NEEDED names first-party shared libraries (without the ".so..."
# suffix, e.g. libmapc_core) that LIBRARY may need in addition to the
# toolchain runtimes.
#
# Header and build-system checks cannot see a dependency created by declaring a
# ROS function by hand (`extern "C" int rcl_init(...);`) and leaving the link to
# whoever links the library. The library's undefined symbols can: any call into
# ROS needs a symbol that only ROS libraries define. Names are demangled, and a
# symbol is rejected if it is a ROS C API function or variable (rcutils_*,
# g_rcutils_*, ...), a rosidl-generated C name (std_msgs__msg__String__init),
# or mentions a ROS C++ namespace anywhere (including template arguments).
#
# For a shared library, the dynamic section is checked too: a ROS library in
# DT_NEEDED is a dependency even if no ROS symbol is left undefined.
#
# Run-time loading (dlopen/dlsym and friends) is rejected as well: it would let
# core reach ROS by name without any link-time symbol. The design uses
# compile-time strategy factories, so core never needs to load code itself.

cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/ros_packages.cmake")

if(NOT DEFINED LIBRARY OR NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "LIBRARY must name a built library (got '${LIBRARY}')")
endif()
if(NOT DEFINED NM OR NM STREQUAL "")
    find_program(NM NAMES nm llvm-nm REQUIRED)
endif()

execute_process(
    COMMAND "${NM}" -C -u "${LIBRARY}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "'${NM} -C -u ${LIBRARY}' failed:\n${_err}")
endif()

# C entry points of the ROS client library, middleware and support packages,
# and C++ namespaces of ROS packages (rclcpp::, std_msgs::msg::, ...).
set(_c_symbol_regex "^${MAPC_ROS_C_PREFIX_REGEX}")
set(_rosidl_c_regex "(^|[^A-Za-z0-9_])${MAPC_ROS_PACKAGE_REGEX}__")
set(_cpp_symbol_regex "(^|[^A-Za-z0-9_])${MAPC_ROS_PACKAGE_REGEX}::")
set(_dynamic_loading_regex "^(dlopen|dlmopen|dlsym|dlvsym|LoadLibrary[AW]?|GetProcAddress)(@.*)?$")

string(REPLACE ";" "\\;" _out "${_out}")
string(REPLACE "\n" ";" _lines "${_out}")

set(_member "${LIBRARY}")
set(_members 0)
set(_undefined 0)
set(_violations "")
foreach(_line IN LISTS _lines)
    if(_line MATCHES "^(.+):$")
        set(_member "${CMAKE_MATCH_1}")
        math(EXPR _members "${_members} + 1")
    elseif(_line MATCHES "^ +[Uw] (.+)$")
        set(_symbol "${CMAKE_MATCH_1}")
        math(EXPR _undefined "${_undefined} + 1")
        if(_symbol MATCHES "${_c_symbol_regex}" OR _symbol MATCHES "${_rosidl_c_regex}"
           OR _symbol MATCHES "${_cpp_symbol_regex}")
            list(APPEND _violations "${_member}: ${_symbol}")
        elseif(_symbol MATCHES "${_dynamic_loading_regex}")
            list(APPEND _violations "${_member}: ${_symbol} (run-time code loading is not allowed in core)")
        endif()
    endif()
endforeach()

# Shared libraries: every DT_NEEDED entry must be a toolchain or sanitizer
# runtime library.
set(_needed_count 0)
set(_allowed_needed "")
if(DEFINED ALLOWED_NEEDED)
    string(REPLACE "|" ";" _allowed_needed "${ALLOWED_NEEDED}")
endif()
if(LIBRARY MATCHES "\\.so(\\.[0-9.]+)?$")
    if(NOT DEFINED READELF OR READELF STREQUAL "")
        find_program(READELF NAMES readelf llvm-readelf REQUIRED)
    endif()
    execute_process(
        COMMAND "${READELF}" -d "${LIBRARY}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _dyn ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "'${READELF} -d ${LIBRARY}' failed:\n${_err}")
    endif()
    set(_runtime_regex "^(libstdc\\+\\+|libc\\+\\+|libc\\+\\+abi|libunwind|libm|libc|libgcc_s|libpthread|libdl|librt|libatomic|ld-linux[-a-z0-9_.]*|libasan|libubsan|libtsan|liblsan)\\.so")
    string(REGEX MATCHALL "\\(NEEDED\\)[^[]*\\[[^]]+\\]" _needed "${_dyn}")
    foreach(_entry IN LISTS _needed)
        string(REGEX REPLACE ".*\\[([^]]+)\\]$" "\\1" _soname "${_entry}")
        math(EXPR _needed_count "${_needed_count} + 1")
        set(_first_party FALSE)
        foreach(_allowed IN LISTS _allowed_needed)
            if(_soname MATCHES "^${_allowed}\\.so")
                set(_first_party TRUE)
            endif()
        endforeach()
        if(NOT _soname MATCHES "${_runtime_regex}" AND NOT _first_party)
            list(APPEND _violations "${LIBRARY}: DT_NEEDED ${_soname} is not a toolchain runtime library")
        endif()
    endforeach()
endif()

if(_undefined EQUAL 0)
    message(FATAL_ERROR "No undefined symbols listed for ${LIBRARY}; is it the right file?")
endif()

if(_violations)
    list(JOIN _violations "\n  " _report)
    message(FATAL_ERROR "${LIBRARY} depends on ROS or loads code at run time:\n  ${_report}")
endif()

message(STATUS "Checked ${_undefined} undefined symbol(s) and ${_needed_count} DT_NEEDED entries of ${LIBRARY}: none from ROS")
