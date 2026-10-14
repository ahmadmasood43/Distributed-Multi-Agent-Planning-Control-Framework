# Fails if any source or CMake file under SCAN_DIR depends on ROS.
#
#   cmake -DSCAN_DIR=<dir> [-DALL_FILES=ON] -P tools/check_no_ros_in_core.cmake
#
# By default C/C++ files are selected by extension. With ALL_FILES=ON every
# regular file is scanned as C/C++ regardless of its name (extensionless
# headers included), except CMake files, which get the CMake scan, and known
# binary artifacts (libraries, objects). Use it for trees whose content is
# published as-is, such as generated or installed headers.
#
# The core library must stay usable without a ROS installation; ROS 2 is only
# allowed under adapters/ros2/.
#
# This is a fast, readable first line of defence. The authoritative checks are
# cmake/ArchitectureRules.cmake (what CMake resolved for mapc_core) and
# tools/check_header_deps.cmake (which headers the compiler actually read).
#
# Both languages are tokenised rather than pattern-matched, so that comments
# and string literals can neither hide a real dependency nor fake one:
#  - C/C++: comments, string/char/raw-string literals and digit separators are
#    understood. Genuine `#include` directives are inspected, and so are the
#    identifiers in code: ROS C API names (rcl_init, g_rcutils_...), rosidl C
#    names (std_msgs__msg__...), ROS C++ namespaces (rclcpp::, namespace
#    std_msgs). That covers ROS functions declared by hand, including in
#    inline or template code that the core library itself never instantiates.
#  - CMake: line and bracket comments (#[==[ ]==]), quoted and bracket
#    arguments are understood, and command names match case-insensitively.

cmake_minimum_required(VERSION 3.21)  # modern policies in script mode

if(NOT DEFINED SCAN_DIR OR NOT IS_DIRECTORY "${SCAN_DIR}")
    message(FATAL_ERROR "SCAN_DIR must name an existing directory (got '${SCAN_DIR}')")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ros_packages.cmake")
set(_ros_pkg "${MAPC_ROS_PACKAGE_REGEX}")

# ---------------------------------------------------------------------------
# C / C++ (tokeniser shared with tools/check_header_deps.cmake)
# ---------------------------------------------------------------------------

include("${CMAKE_CURRENT_LIST_DIR}/cpp_scan.cmake")

# ---------------------------------------------------------------------------
# CMake
# ---------------------------------------------------------------------------

# Returns `content` with comments removed, the contents of quoted and bracket
# arguments neutralised (parentheses dropped, so text inside a string can never
# form a command), and everything lower-cased (CMake command names are
# case-insensitive; ROS package names are lower case).
function(_mapc_cmake_sanitise content out_var)
    set(_out "")
    set(_rest "${content}")

    while(NOT _rest STREQUAL "")
        string(SUBSTRING "${_rest}" 0 1 _c)

        if(_c STREQUAL "#")
            if(_rest MATCHES "^#\\[(=*)\\[")
                # Bracket comment #[==[ ... ]==]
                set(_close "]${CMAKE_MATCH_1}]")
                string(LENGTH "${CMAKE_MATCH_0}" _open_len)
                string(SUBSTRING "${_rest}" ${_open_len} -1 _after)
                string(FIND "${_after}" "${_close}" _end)
                if(_end EQUAL -1)
                    set(_rest "")
                else()
                    string(LENGTH "${_close}" _close_len)
                    math(EXPR _end "${_end} + ${_close_len}")
                    string(SUBSTRING "${_after}" ${_end} -1 _rest)
                endif()
                string(APPEND _out " ")
            else()
                string(FIND "${_rest}" "\n" _end)
                if(_end EQUAL -1)
                    set(_rest "")
                else()
                    string(SUBSTRING "${_rest}" ${_end} -1 _rest)
                endif()
            endif()
        elseif(_c STREQUAL "\"")
            if(_rest MATCHES "^\"([^\"\\\\]|\\\\.)*\"")
                set(_arg "${CMAKE_MATCH_0}")
            else()
                set(_arg "${_rest}")  # unterminated
            endif()
            string(LENGTH "${_arg}" _n)
            string(SUBSTRING "${_rest}" ${_n} -1 _rest)
            string(REGEX REPLACE "[()]" " " _arg "${_arg}")
            string(APPEND _out "${_arg}")
        elseif(_c STREQUAL "\\")
            # Escape sequence in an unquoted argument (e.g. \# or \)): it is
            # argument text, never a comment or structural parenthesis.
            string(LENGTH "${_rest}" _len)
            if(_len GREATER 1)
                string(SUBSTRING "${_rest}" 2 -1 _rest)
            else()
                set(_rest "")
            endif()
            string(APPEND _out "__")
        elseif(_c STREQUAL "[" AND _rest MATCHES "^\\[(=*)\\[")
            # Bracket argument [==[ ... ]==]
            set(_close "]${CMAKE_MATCH_1}]")
            string(LENGTH "${CMAKE_MATCH_0}" _open_len)
            string(SUBSTRING "${_rest}" ${_open_len} -1 _after)
            string(FIND "${_after}" "${_close}" _end)
            if(_end EQUAL -1)
                set(_arg "${_after}")
                set(_rest "")
            else()
                string(SUBSTRING "${_after}" 0 ${_end} _arg)
                string(LENGTH "${_close}" _close_len)
                math(EXPR _end "${_end} + ${_close_len}")
                string(SUBSTRING "${_after}" ${_end} -1 _rest)
            endif()
            string(REGEX REPLACE "[()]" " " _arg "${_arg}")
            string(APPEND _out "\"${_arg}\"")
        else()
            string(REGEX MATCH "^[^#\"[\\]+" _chunk "${_rest}")
            if(_chunk STREQUAL "")
                set(_chunk "${_c}")  # a '[' that does not open a bracket argument
            endif()
            string(LENGTH "${_chunk}" _n)
            string(SUBSTRING "${_rest}" ${_n} -1 _rest)
            string(APPEND _out "${_chunk}")
        endif()
    endwhile()

    # Parentheses nested inside a command's argument list are argument text
    # (e.g. `message(STATUS find_package(x))`), not commands: blank them so
    # only top-level invocations can match.
    set(_flat "")
    set(_depth 0)
    set(_rest "${_out}")
    while(NOT _rest STREQUAL "")
        string(REGEX MATCH "^[^()]+" _chunk "${_rest}")
        if(NOT _chunk STREQUAL "")
            string(APPEND _flat "${_chunk}")
            string(LENGTH "${_chunk}" _n)
        else()
            string(SUBSTRING "${_rest}" 0 1 _p)
            set(_n 1)
            if(_p STREQUAL "(")
                if(_depth EQUAL 0)
                    string(APPEND _flat "(")
                else()
                    string(APPEND _flat " ")
                endif()
                math(EXPR _depth "${_depth} + 1")
            else()
                if(_depth GREATER 0)
                    math(EXPR _depth "${_depth} - 1")
                endif()
                if(_depth EQUAL 0)
                    string(APPEND _flat ")")
                else()
                    string(APPEND _flat " ")
                endif()
            endif()
        endif()
        string(SUBSTRING "${_rest}" ${_n} -1 _rest)
    endwhile()

    string(TOLOWER "${_flat}" _flat)
    set(${out_var} "${_flat}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Scan
# ---------------------------------------------------------------------------

# Every C/C++ source, header and implementation-header extension in use, plus
# configure_file() templates.
set(_cpp_extensions c cc cpp cxx c++ cppm ixx h hh hpp hxx h++ ipp tpp tcc inl inc in)
set(_source_globs "")
foreach(_ext IN LISTS _cpp_extensions)
    list(APPEND _source_globs "${SCAN_DIR}/*.${_ext}")
endforeach()
file(GLOB_RECURSE _cmake_files "${SCAN_DIR}/CMakeLists.txt" "${SCAN_DIR}/*.cmake")
if(ALL_FILES)
    file(GLOB_RECURSE _all LIST_DIRECTORIES false "${SCAN_DIR}/*")
    set(_sources "")
    foreach(_file IN LISTS _all)
        if(_file IN_LIST _cmake_files)
            continue()
        endif()
        if(_file MATCHES "\\.(a|so|dylib|dll|lib|o|obj|pdb|exe)$" OR _file MATCHES "\\.so\\.[0-9.]+$")
            continue()
        endif()
        list(APPEND _sources "${_file}")
    endforeach()
else()
    file(GLOB_RECURSE _sources ${_source_globs})
endif()

set(_ws "[ \t\r\n]")
# CMake's grammar puts each command invocation at the start of a line (after
# optional whitespace), so text that merely looks like a command inside another
# command's arguments is not matched.
set(_cmd "(^|\n)[ \t]*")
set(_find_regex "${_cmd}(find_package|find_dependency)${_ws}*\\(${_ws}*\"?${_ros_pkg}[\" \t\r\n)]")
set(_ament_regex "${_cmd}ament_target_dependencies${_ws}*\\(([^)]*[ \t\r\n\"])?${_ros_pkg}[\" \t\r\n)]")
set(_link_regex "${_cmd}target_link_libraries${_ws}*\\([^)]*${_ros_pkg}(::[a-z0-9_]+|_libraries|_targets)")

set(_violations "")

foreach(_file IN LISTS _sources)
    file(READ "${_file}" _content)
    mapc_cpp_scan("${_content}" _scan)
    foreach(_entry IN LISTS _scan_INCLUDES)
        string(REGEX MATCH "^([0-9]+)\\|(.*)$" _ "${_entry}")
        set(_line "${CMAKE_MATCH_1}")
        set(_name "${CMAKE_MATCH_2}")
        if(_name MATCHES "^[<\"]${_ros_pkg}/")
            list(APPEND _violations "${_file}:${_line}: #include ${_name}")
        endif()
    endforeach()
    foreach(_entry IN LISTS _scan_IDENTIFIERS)
        string(REGEX MATCH "^([0-9]+)\\|(.*)$" _ "${_entry}")
        list(APPEND _violations "${_file}:${CMAKE_MATCH_1}: uses ROS identifier '${CMAKE_MATCH_2}'")
    endforeach()
endforeach()

foreach(_file IN LISTS _cmake_files)
    file(READ "${_file}" _content)
    _mapc_cmake_sanitise("${_content}" _sanitised)
    foreach(_regex IN ITEMS "${_find_regex}" "${_ament_regex}" "${_link_regex}")
        string(REGEX MATCHALL "${_regex}" _hits "${_sanitised}")
        foreach(_hit IN LISTS _hits)
            string(REGEX REPLACE "^[ \t\r\n]+" "" _hit "${_hit}")
            string(REGEX REPLACE "[ \t\r\n]+" " " _hit "${_hit}")
            list(APPEND _violations "${_file}: ${_hit}")
        endforeach()
    endforeach()
endforeach()

if(_violations)
    list(JOIN _violations "\n  " _report)
    message(FATAL_ERROR "ROS dependencies found under ${SCAN_DIR}:\n  ${_report}")
endif()

list(LENGTH _sources _n_sources)
list(LENGTH _cmake_files _n_cmake)
message(STATUS "No ROS dependencies in ${SCAN_DIR} (${_n_sources} sources, ${_n_cmake} CMake files checked)")
