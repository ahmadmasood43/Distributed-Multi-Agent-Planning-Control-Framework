# Fails if any object file of the core library was compiled with a ROS header,
# or with a core-owned file that uses ROS.
#
#   cmake -DBUILD_DIR=<ninja build dir> [-DNINJA=<ninja>] [-DTARGETS=<t1;t2>]
#         [-DOWNED_ROOTS=<dir1|dir2>] -P tools/check_header_deps.cmake
#   cmake -DDEPS_FILE=<saved `ninja -t deps` output> [-DOBJECT_REGEX=<re>]
#         -P tools/check_header_deps.cmake
#
# This is the compiler's ground truth: Ninja records every header each object
# actually read (from the compiler's depfiles), after macro expansion, line
# splicing and comment handling. No spelling of an #include can hide from it.
#
# Which objects to check is not guessed from directory names: in BUILD_DIR mode
# the object files are the real inputs of TARGETS (`ninja -t query`), so
# objects contributed by object libraries are included. Every one of them must
# have a VALID dependency record. Header paths are checked both as recorded and
# with symlinks resolved, so an innocently named alias of a ROS header is
# caught. Requires the Ninja generator and a completed build.
#
# Every file the compiler read that lies inside OWNED_ROOTS (the core sources
# and the generated include directory) is also run through the source scanner
# (tools/cpp_scan.cmake): this covers generated headers and implementation
# headers of any extension, including inline/template code the library never
# instantiates and which therefore leaves no symbol behind.

cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/ros_packages.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cpp_scan.cmake")

set(_owned_roots "")
if(DEFINED OWNED_ROOTS AND NOT OWNED_ROOTS STREQUAL "")
    string(REPLACE "|" ";" _owned_roots_raw "${OWNED_ROOTS}")
    foreach(_root IN LISTS _owned_roots_raw)
        if(EXISTS "${_root}")
            file(REAL_PATH "${_root}" _real_root)
            list(APPEND _owned_roots "${_real_root}")
        endif()
    endforeach()
endif()
set(_owned_files "")

set(_base_dir "")
set(_members "")

if(DEFINED DEPS_FILE)
    # Saved records carry no build graph, so membership is by object path.
    if(NOT DEFINED OBJECT_REGEX)
        set(OBJECT_REGEX "(^|/)(core/CMakeFiles/mapc_core\\.dir|tests/CMakeFiles/mapc_header_check\\.dir)/")
    endif()
    file(READ "${DEPS_FILE}" _deps)
elseif(DEFINED BUILD_DIR)
    if(NOT DEFINED NINJA)
        find_program(NINJA NAMES ninja ninja-build REQUIRED)
    endif()
    if(NOT DEFINED TARGETS)
        # The core library plus the per-header self-containment objects, which
        # are the only translation units that compile header-only core code.
        set(TARGETS mapc_core mapc_header_check)
    endif()
    file(REAL_PATH "${BUILD_DIR}" _base_dir)

    # Walk the build graph from each target through its explicit inputs
    # (`ninja -t query`) until object files are reached. This follows phony
    # aliases, archives and object libraries alike.
    foreach(_target IN LISTS TARGETS)
        set(_queue "${_target}")
        set(_visited "")
        set(_found 0)
        while(_queue)
            list(POP_FRONT _queue _node)
            if(_node IN_LIST _visited)
                continue()
            endif()
            list(APPEND _visited "${_node}")
            execute_process(
                COMMAND "${NINJA}" -C "${BUILD_DIR}" -t query "${_node}"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _query ERROR_VARIABLE _err)
            if(NOT _rc EQUAL 0)
                message(FATAL_ERROR "'ninja -t query ${_node}' failed in ${BUILD_DIR}:\n${_err}")
            endif()
            string(REPLACE ";" "\\;" _query "${_query}")
            string(REPLACE "\n" ";" _query "${_query}")
            set(_in_inputs FALSE)
            foreach(_qline IN LISTS _query)
                if(_qline MATCHES "^  input: ")
                    set(_in_inputs TRUE)
                elseif(_qline MATCHES "^  [a-z]")
                    set(_in_inputs FALSE)  # "outputs:" or "validations:"
                elseif(_in_inputs AND _qline MATCHES "^    ([^|].*)$")
                    # Explicit inputs only ("| " implicit and "|| " order-only
                    # inputs are build-ordering edges, not archive members).
                    set(_input "${CMAKE_MATCH_1}")
                    if(_input MATCHES "\\.(o|obj)$")
                        list(APPEND _members "${_input}")
                        math(EXPR _found "${_found} + 1")
                    else()
                        list(APPEND _queue "${_input}")
                    endif()
                endif()
            endforeach()
        endwhile()
        if(_found EQUAL 0)
            message(FATAL_ERROR "Target '${_target}' has no object-file inputs in ${BUILD_DIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _members)

    execute_process(
        COMMAND "${NINJA}" -C "${BUILD_DIR}" -t deps ${_members}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _deps
        ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "'ninja -t deps' failed in ${BUILD_DIR}:\n${_err}")
    endif()
else()
    message(FATAL_ERROR "Pass BUILD_DIR (a Ninja build directory) or DEPS_FILE")
endif()

# A header path belongs to ROS if it lies in a ROS installation (/opt/ros/...)
# or in a ROS package's include/install tree (colcon workspaces, vendored copies).
set(_ros_path_regex "(^|/)opt/ros/|/(include|install)/${MAPC_ROS_PACKAGE_REGEX}/")

string(REPLACE ";" "\\;" _deps "${_deps}")
string(REPLACE "\n" ";" _lines "${_deps}")

set(_object "")
set(_checked 0)
set(_violations "")

foreach(_line IN LISTS _lines)
    if(_line MATCHES "^([^ ].*): #deps [0-9]+, deps mtime [0-9]+ \\(([A-Z]+)\\)$")
        set(_object "")
        set(_name "${CMAKE_MATCH_1}")
        set(_state "${CMAKE_MATCH_2}")
        if((_members AND _name IN_LIST _members) OR (NOT _members AND _name MATCHES "${OBJECT_REGEX}"))
            if(NOT _state STREQUAL "VALID")
                message(FATAL_ERROR "Dependency information for ${_name} is ${_state}; rebuild first")
            endif()
            set(_object "${_name}")
            math(EXPR _checked "${_checked} + 1")
        endif()
    elseif(_object AND _line MATCHES "^    (.+)$")
        set(_header "${CMAKE_MATCH_1}")
        string(TOLOWER "${_header}" _lower)
        set(_resolved "")
        if(_base_dir)
            # Resolve symlinks (relative records are relative to the build dir).
            file(REAL_PATH "${_header}" _resolved BASE_DIRECTORY "${_base_dir}")
        endif()
        string(TOLOWER "${_resolved}" _resolved_lower)
        if(_lower MATCHES "${_ros_path_regex}")
            list(APPEND _violations "${_object}: ${_header}")
        elseif(_resolved_lower MATCHES "${_ros_path_regex}")
            list(APPEND _violations "${_object}: ${_header} -> ${_resolved}")
        endif()
        if(_owned_roots AND _resolved)
            foreach(_root IN LISTS _owned_roots)
                file(RELATIVE_PATH _rel "${_root}" "${_resolved}")
                if(NOT _rel MATCHES "^\\.\\.(/|$)" AND NOT IS_ABSOLUTE "${_rel}")
                    list(APPEND _owned_files "${_resolved}")
                    break()
                endif()
            endforeach()
        endif()
    endif()
endforeach()

# Scan the core-owned files the compiler actually read.
list(REMOVE_DUPLICATES _owned_files)
foreach(_file IN LISTS _owned_files)
    file(READ "${_file}" _content)
    mapc_cpp_scan("${_content}" _scan)
    foreach(_entry IN LISTS _scan_INCLUDES)
        string(REGEX MATCH "^([0-9]+)\\|(.*)$" _ "${_entry}")
        if(CMAKE_MATCH_2 MATCHES "^[<\"]${MAPC_ROS_PACKAGE_REGEX}/")
            list(APPEND _violations "${_file}:${CMAKE_MATCH_1}: #include ${CMAKE_MATCH_2}")
        endif()
    endforeach()
    foreach(_entry IN LISTS _scan_IDENTIFIERS)
        string(REGEX MATCH "^([0-9]+)\\|(.*)$" _ "${_entry}")
        list(APPEND _violations "${_file}:${CMAKE_MATCH_1}: uses ROS identifier '${CMAKE_MATCH_2}'")
    endforeach()
endforeach()

if(_members)
    list(LENGTH _members _expected)
    if(NOT _checked EQUAL _expected)
        message(FATAL_ERROR
            "Only ${_checked} of ${_expected} object files of '${TARGETS}' have dependency "
            "records; rebuild with the Ninja generator before running this check.")
    endif()
endif()

if(_checked EQUAL 0)
    message(FATAL_ERROR
        "No dependency information found for objects matching '${OBJECT_REGEX}'. "
        "Build the project with the Ninja generator before running this check.")
endif()

if(_violations)
    list(JOIN _violations "\n  " _report)
    message(FATAL_ERROR "Core objects were compiled with ROS headers or ROS-using core files:\n  ${_report}")
endif()

list(LENGTH _owned_files _n_owned)
message(STATUS "Checked the headers of ${_checked} core object file(s) and scanned ${_n_owned} core-owned file(s): no ROS dependencies")
