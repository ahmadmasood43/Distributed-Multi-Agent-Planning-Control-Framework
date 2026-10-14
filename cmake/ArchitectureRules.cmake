# Authoritative dependency rules for first-party targets.
#
# tools/check_no_ros_in_core.cmake scans source text and gives early, readable
# feedback, but text scanning can never be complete. This module checks what
# CMake resolved instead: a target and every target it depends on may only use
#
#   - link items that are either ALLOWED_TARGETS or ALLOWED_LINK_ITEMS, through
#     any link property, including INTERFACE_LINK_LIBRARIES_DIRECT and the
#     legacy LINK_INTERFACE_LIBRARIES. ALLOWED_TARGETS must be targets visible
#     to the check, and their usage requirements are followed transitively (so
#     an allowed target cannot smuggle in other dependencies). ALLOWED_LINK_ITEMS
#     are plain flags/library names (e.g. -pthread) and must never name a
#     target, neither at configure time nor at generate time (fail closed: a
#     target created later under an allowed plain name cannot be inspected);
#   - link options from ALLOWED_LINK_OPTIONS; no legacy LINK_FLAGS or
#     STATIC_LIBRARY_FLAGS and no link directories;
#   - compile options that cannot add include paths, forced includes or
#     libraries (-I, -isystem, -include, -L, -l, -Wl, LINKER:, SHELL:, @file...);
#   - build-interface include directories inside ALLOWED_INCLUDE_ROOTS, and
#     install-interface include directories listed in ALLOWED_INSTALL_INCLUDES;
#   - source files and precompiled headers inside ALLOWED_SOURCE_ROOTS (so no
#     $<TARGET_OBJECTS:...> or other generator expressions that pull in
#     foreign object files).
#
# Generator expressions are unwrapped only for $<BUILD_INTERFACE:...>,
# $<INSTALL_INTERFACE:...> and $<LINK_ONLY:...>; any other generator expression
# is rejected (fail closed), because its value cannot be checked here.
#
# The rules are applied twice:
#   1. at the end of the owning project's top directory (early, readable
#      configure error), and
#   2. on the requirements *as generated*: the configure-time check registers a
#      file(GENERATE) dump of the final target properties, which is written
#      after every configure-time callback (including deferred ones) has run.
#      tools/check_generated_requirements.cmake applies the same rules to it.
#
#   mapc_check_target_dependencies(<target>
#       [ALLOWED_TARGETS <target>...]
#       [ALLOWED_LINK_ITEMS <item>...]
#       [ALLOWED_LINK_OPTIONS <option>...]
#       [ALLOWED_INCLUDE_ROOTS <dir>...]
#       [ALLOWED_INSTALL_INCLUDES <path>...]
#       [ALLOWED_SOURCE_ROOTS <dir>...]
#       [DUMP_FILE <path>]             # register the generate-time dump
#       [RESULT_VARIABLE <var>])       # report instead of failing (for tests)
#
#   mapc_defer_dependency_check(<target> <same arguments>)
#       Runs the check at the end of the current project's top directory (where
#       every target the project relies on is visible), with the arguments'
#       current values. Anything a consuming project changes afterwards is
#       outside the guarantee, but still shows up in the generated dump.

# Property groups. The checked target (ROOT) contributes its own build
# requirements; targets it depends on (DEP) contribute only their usage
# (INTERFACE_*) requirements. A macro rather than file-scope variables because
# the checks also run in other directory scopes (deferred calls) and in script
# mode.
macro(_mapc_property_groups)
    set(_MAPC_ROOT_LINK_PROPS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES INTERFACE_LINK_LIBRARIES_DIRECT
        LINK_INTERFACE_LIBRARIES LINK_INTERFACE_LIBRARIES_DEBUG LINK_INTERFACE_LIBRARIES_RELEASE
        LINK_INTERFACE_LIBRARIES_RELWITHDEBINFO LINK_INTERFACE_LIBRARIES_MINSIZEREL)
    set(_MAPC_DEP_LINK_PROPS INTERFACE_LINK_LIBRARIES INTERFACE_LINK_LIBRARIES_DIRECT)
    set(_MAPC_ROOT_OPTION_PROPS LINK_OPTIONS INTERFACE_LINK_OPTIONS STATIC_LIBRARY_OPTIONS)
    set(_MAPC_DEP_OPTION_PROPS INTERFACE_LINK_OPTIONS)
    set(_MAPC_ROOT_FLAG_PROPS LINK_FLAGS LINK_FLAGS_DEBUG LINK_FLAGS_RELEASE LINK_FLAGS_RELWITHDEBINFO
        LINK_FLAGS_MINSIZEREL STATIC_LIBRARY_FLAGS STATIC_LIBRARY_FLAGS_DEBUG
        STATIC_LIBRARY_FLAGS_RELEASE STATIC_LIBRARY_FLAGS_RELWITHDEBINFO
        STATIC_LIBRARY_FLAGS_MINSIZEREL)
    set(_MAPC_ROOT_LINKDIR_PROPS LINK_DIRECTORIES INTERFACE_LINK_DIRECTORIES)
    set(_MAPC_DEP_LINKDIR_PROPS INTERFACE_LINK_DIRECTORIES)
    set(_MAPC_ROOT_COMPILE_PROPS COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
    set(_MAPC_DEP_COMPILE_PROPS INTERFACE_COMPILE_OPTIONS)
    set(_MAPC_ROOT_INCLUDE_PROPS INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    set(_MAPC_DEP_INCLUDE_PROPS INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    set(_MAPC_ROOT_SOURCE_PROPS SOURCES INTERFACE_SOURCES PRECOMPILE_HEADERS
        INTERFACE_PRECOMPILE_HEADERS)
    set(_MAPC_DEP_SOURCE_PROPS INTERFACE_SOURCES INTERFACE_PRECOMPILE_HEADERS)
endmacro()

function(_mapc_unwrap_genex value out_var)
    set(_v "${value}")
    set(_install FALSE)
    while(_v MATCHES "^\\$<(BUILD_INTERFACE|INSTALL_INTERFACE|LINK_ONLY):(.*)>$")
        if(CMAKE_MATCH_1 STREQUAL "INSTALL_INTERFACE")
            set(_install TRUE)
        endif()
        set(_v "${CMAKE_MATCH_2}")
    endwhile()
    set(${out_var} "${_v}" PARENT_SCOPE)
    set(${out_var}_INSTALL ${_install} PARENT_SCOPE)
endfunction()

# Sets <out_var> to TRUE if absolute path <path> is inside one of <roots>
# (symlinks resolved on both sides).
function(_mapc_path_inside path out_var)
    set(_inside FALSE)
    file(REAL_PATH "${path}" _real)
    foreach(_root IN LISTS ARGN)
        file(REAL_PATH "${_root}" _real_root)
        file(RELATIVE_PATH _rel "${_real_root}" "${_real}")
        if(NOT _rel MATCHES "^\\.\\.(/|$)" AND NOT IS_ABSOLUTE "${_rel}")
            set(_inside TRUE)
        endif()
    endforeach()
    set(${out_var} ${_inside} PARENT_SCOPE)
endfunction()

# Property access that works both on live targets (configure time) and on a
# generate-time dump (script mode, where MAPC_DUMP_MODE is set and the dump
# defines MAPC_DUMP_<target>_<property>, MAPC_DUMP_IS_TARGET_<name> and
# MAPC_DUMP_ALIAS_<name>).
function(_mapc_get_prop target prop out_var)
    if(MAPC_DUMP_MODE)
        set(_v "${MAPC_DUMP_${target}_${prop}}")
    else()
        get_target_property(_v ${target} ${prop})
        if(NOT _v)
            set(_v "")
        endif()
    endif()
    set(${out_var} "${_v}" PARENT_SCOPE)
endfunction()

function(_mapc_resolve_target name out_var)
    set(_resolved "")
    if(MAPC_DUMP_MODE)
        if(DEFINED MAPC_DUMP_ALIAS_${name})
            set(_resolved "${MAPC_DUMP_ALIAS_${name}}")
        elseif(MAPC_DUMP_IS_TARGET_${name})
            set(_resolved "${name}")
        endif()
    elseif(TARGET "${name}")
        get_target_property(_aliased "${name}" ALIASED_TARGET)
        if(_aliased)
            set(_resolved "${_aliased}")
        else()
            set(_resolved "${name}")
        endif()
    endif()
    set(${out_var} "${_resolved}" PARENT_SCOPE)
endfunction()

function(_mapc_target_exists name out_var)
    if(MAPC_DUMP_MODE)
        string(MAKE_C_IDENTIFIER "${name}" _id)
        set(_exists "${MAPC_DUMP_EXISTS_${_id}}")
    elseif(TARGET "${name}")
        set(_exists 1)
    else()
        set(_exists 0)
    endif()
    if(_exists STREQUAL "1")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# The rules engine. Returns the list of violations and the targets visited.
function(_mapc_evaluate_rules target violations_var visited_var)
    cmake_parse_arguments(ARG "" ""
        "ALLOWED_TARGETS;ALLOWED_LINK_ITEMS;ALLOWED_LINK_OPTIONS;ALLOWED_INCLUDE_ROOTS;ALLOWED_INSTALL_INCLUDES;ALLOWED_SOURCE_ROOTS"
        ${ARGN})

    # Compile options that can add include paths, forced includes, libraries
    # or linker input.
    set(_forbidden_compile_option_regex
        "^(-I|-isystem|-iquote|-idirafter|-imacros|-include|--include|-iprefix|-iwithprefix|-isysroot|--sysroot|-B|-L|-l|-Wl,|-Xlinker|LINKER:|SHELL:|@|/I|/FI|-fplugin)")

    _mapc_property_groups()
    set(_violations "")
    set(_queue "${target}")
    set(_visited "")

    while(_queue)
        list(POP_FRONT _queue _t)
        _mapc_resolve_target("${_t}" _resolved)
        if(_resolved)
            set(_t "${_resolved}")
        endif()
        if(_t IN_LIST _visited)
            continue()
        endif()
        list(APPEND _visited "${_t}")

        if(_t STREQUAL target)
            set(_kind ROOT)
        else()
            set(_kind DEP)
        endif()

        foreach(_prop IN LISTS _MAPC_${_kind}_LINK_PROPS)
            _mapc_get_prop("${_t}" ${_prop} _items)
            foreach(_item IN LISTS _items)
                # CMake brackets items added from another directory with
                # internal "::@(<address>)" / "::@" scope markers.
                if(_item MATCHES "^::@(\\(0x[0-9A-Fa-f]+\\))?$")
                    continue()
                endif()
                _mapc_unwrap_genex("${_item}" _plain)
                if(_plain MATCHES "\\$<")
                    list(APPEND _violations "${_t} ${_prop}: unsupported generator expression '${_item}'")
                elseif(_plain IN_LIST ARG_ALLOWED_TARGETS)
                    _mapc_resolve_target("${_plain}" _dep)
                    if(_dep)
                        list(APPEND _queue "${_dep}")
                    else()
                        list(APPEND _violations
                            "${_t} ${_prop}: allowed target '${_plain}' is not visible to the check (a directory-scoped imported target?), so its usage requirements cannot be inspected")
                    endif()
                elseif(_plain IN_LIST ARG_ALLOWED_LINK_ITEMS)
                    _mapc_target_exists("${_plain}" _exists)
                    if(_exists)
                        list(APPEND _violations
                            "${_t} ${_prop}: '${_plain}' is allowed only as a plain link item, but a target with that name exists")
                    endif()
                else()
                    list(APPEND _violations "${_t} ${_prop}: '${_plain}' is not an allowed dependency")
                endif()
            endforeach()
        endforeach()

        foreach(_prop IN LISTS _MAPC_${_kind}_OPTION_PROPS)
            _mapc_get_prop("${_t}" ${_prop} _items)
            foreach(_item IN LISTS _items)
                if(NOT _item IN_LIST ARG_ALLOWED_LINK_OPTIONS)
                    list(APPEND _violations "${_t} ${_prop}: '${_item}' is not an allowed link option")
                endif()
            endforeach()
        endforeach()

        if(_kind STREQUAL "ROOT")
            foreach(_prop IN LISTS _MAPC_ROOT_FLAG_PROPS)
                _mapc_get_prop("${_t}" ${_prop} _items)
                if(NOT _items STREQUAL "")
                    list(APPEND _violations "${_t} ${_prop}: legacy flag properties are not allowed ('${_items}')")
                endif()
            endforeach()
        endif()

        foreach(_prop IN LISTS _MAPC_${_kind}_LINKDIR_PROPS)
            _mapc_get_prop("${_t}" ${_prop} _items)
            if(NOT _items STREQUAL "")
                list(APPEND _violations "${_t} ${_prop}: link directories are not allowed ('${_items}')")
            endif()
        endforeach()

        foreach(_prop IN LISTS _MAPC_${_kind}_COMPILE_PROPS)
            _mapc_get_prop("${_t}" ${_prop} _items)
            foreach(_item IN LISTS _items)
                _mapc_unwrap_genex("${_item}" _plain)
                if(_plain MATCHES "\\$<")
                    list(APPEND _violations "${_t} ${_prop}: unsupported generator expression '${_item}'")
                elseif(_plain MATCHES "${_forbidden_compile_option_regex}")
                    list(APPEND _violations "${_t} ${_prop}: '${_plain}' can add external dependencies")
                endif()
            endforeach()
        endforeach()

        foreach(_prop IN LISTS _MAPC_${_kind}_INCLUDE_PROPS)
            _mapc_get_prop("${_t}" ${_prop} _dirs)
            foreach(_dir IN LISTS _dirs)
                _mapc_unwrap_genex("${_dir}" _plain)
                if(_plain MATCHES "\\$<")
                    list(APPEND _violations "${_t} ${_prop}: unsupported generator expression '${_dir}'")
                elseif(_plain_INSTALL)
                    # Install-interface paths must be exactly the declared
                    # installed include directories.
                    if(NOT _plain IN_LIST ARG_ALLOWED_INSTALL_INCLUDES)
                        list(APPEND _violations "${_t} ${_prop}: install include '${_plain}' is not allowed")
                    endif()
                elseif(NOT IS_ABSOLUTE "${_plain}")
                    list(APPEND _violations "${_t} ${_prop}: relative include directory '${_plain}'")
                else()
                    _mapc_path_inside("${_plain}" _inside ${ARG_ALLOWED_INCLUDE_ROOTS})
                    if(NOT _inside)
                        list(APPEND _violations "${_t} ${_prop}: '${_plain}' is outside the allowed include roots")
                    endif()
                endif()
            endforeach()
        endforeach()

        _mapc_get_prop("${_t}" SOURCE_DIR _source_dir)
        foreach(_prop IN LISTS _MAPC_${_kind}_SOURCE_PROPS)
            _mapc_get_prop("${_t}" ${_prop} _sources)
            foreach(_src IN LISTS _sources)
                _mapc_unwrap_genex("${_src}" _plain)
                if(_plain MATCHES "\\$<")
                    list(APPEND _violations "${_t} ${_prop}: unsupported generator expression '${_src}'")
                    continue()
                endif()
                if(NOT IS_ABSOLUTE "${_plain}")
                    set(_plain "${_source_dir}/${_plain}")
                endif()
                _mapc_path_inside("${_plain}" _inside ${ARG_ALLOWED_SOURCE_ROOTS})
                if(NOT _inside)
                    list(APPEND _violations "${_t} ${_prop}: '${_plain}' is outside the allowed source roots")
                endif()
            endforeach()
        endforeach()
    endwhile()

    set(${violations_var} "${_violations}" PARENT_SCOPE)
    set(${visited_var} "${_visited}" PARENT_SCOPE)
endfunction()

# Registers a file(GENERATE) dump of the final properties of <targets>, plus
# the rule arguments, for tools/check_generated_requirements.cmake.
function(_mapc_register_dump dump_file root names plain_items rule_args)
    _mapc_property_groups()
    set(_props SOURCE_DIR)
    foreach(_group LINK OPTION LINKDIR COMPILE INCLUDE SOURCE)
        list(APPEND _props ${_MAPC_ROOT_${_group}_PROPS})
    endforeach()
    list(APPEND _props ${_MAPC_ROOT_FLAG_PROPS})
    list(REMOVE_DUPLICATES _props)

    set(_q "]=======]")
    set(_content "# Generated by cmake/ArchitectureRules.cmake. Do not edit.\n")
    string(APPEND _content "set(MAPC_DUMP_ROOT [=======[${root}${_q})\n")
    string(APPEND _content "set(MAPC_DUMP_RULE_ARGS [=======[${rule_args}${_q})\n")
    foreach(_name IN LISTS names)
        if(NOT TARGET "${_name}")
            continue()
        endif()
        get_target_property(_aliased "${_name}" ALIASED_TARGET)
        if(_aliased)
            string(APPEND _content "set(MAPC_DUMP_ALIAS_${_name} [=======[${_aliased}${_q})\n")
            set(_name "${_aliased}")
        endif()
        string(APPEND _content "set(MAPC_DUMP_IS_TARGET_${_name} TRUE)\n")
        get_target_property(_imported "${_name}" IMPORTED)
        foreach(_prop IN LISTS _props)
            # Non-INTERFACE properties cannot be read from INTERFACE or
            # imported targets; those targets only contribute INTERFACE_*.
            if((_imported OR NOT _name STREQUAL root) AND NOT _prop MATCHES "^INTERFACE_|^SOURCE_DIR$")
                continue()
            endif()
            if(_imported AND _prop STREQUAL "SOURCE_DIR")
                continue()
            endif()
            string(APPEND _content
                "set(MAPC_DUMP_${_name}_${_prop} [=======[$<TARGET_PROPERTY:${_name},${_prop}>${_q})\n")
        endforeach()
    endforeach()

    # Whether each allowed plain item names a target in the final build.
    foreach(_item IN LISTS plain_items)
        string(MAKE_C_IDENTIFIER "${_item}" _id)
        string(APPEND _content "set(MAPC_DUMP_EXISTS_${_id} [=======[$<TARGET_EXISTS:${_item}>${_q})\n")
    endforeach()

    file(GENERATE OUTPUT "${dump_file}" CONTENT "${_content}")
endfunction()

function(mapc_check_target_dependencies target)
    cmake_parse_arguments(ARG "" "RESULT_VARIABLE;DUMP_FILE"
        "ALLOWED_TARGETS;ALLOWED_LINK_ITEMS;ALLOWED_LINK_OPTIONS;ALLOWED_INCLUDE_ROOTS;ALLOWED_INSTALL_INCLUDES;ALLOWED_SOURCE_ROOTS"
        ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR "mapc_check_target_dependencies: no target named '${target}'")
    endif()

    set(_rule_args
        ALLOWED_TARGETS ${ARG_ALLOWED_TARGETS}
        ALLOWED_LINK_ITEMS ${ARG_ALLOWED_LINK_ITEMS}
        ALLOWED_LINK_OPTIONS ${ARG_ALLOWED_LINK_OPTIONS}
        ALLOWED_INCLUDE_ROOTS ${ARG_ALLOWED_INCLUDE_ROOTS}
        ALLOWED_INSTALL_INCLUDES ${ARG_ALLOWED_INSTALL_INCLUDES}
        ALLOWED_SOURCE_ROOTS ${ARG_ALLOWED_SOURCE_ROOTS})

    _mapc_evaluate_rules(${target} _violations _visited ${_rule_args})

    if(ARG_DUMP_FILE)
        # Dump the root and every allowed link item that is a target, so the
        # generate-time check can follow the same closure.
        set(_names ${target} ${ARG_ALLOWED_TARGETS})
        _mapc_register_dump("${ARG_DUMP_FILE}" "${target}" "${_names}" "${ARG_ALLOWED_LINK_ITEMS}"
                            "${_rule_args}")
    endif()

    if(ARG_RESULT_VARIABLE)
        set(${ARG_RESULT_VARIABLE} "${_violations}" PARENT_SCOPE)
    elseif(_violations)
        list(JOIN _violations "\n  " _report)
        message(FATAL_ERROR
            "Target '${target}' has dependencies outside its allowlist "
            "(see cmake/ArchitectureRules.cmake):\n  ${_report}")
    endif()
endfunction()

function(mapc_defer_dependency_check target)
    # Arguments are frozen now (as bracket arguments), because a deferred call
    # would otherwise expand variables in the top-level directory's scope.
    set(_code "cmake_language(DEFER DIRECTORY [==[${PROJECT_SOURCE_DIR}]==] CALL mapc_check_target_dependencies [==[${target}]==]")
    foreach(_arg IN LISTS ARGN)
        string(APPEND _code " [==[${_arg}]==]")
    endforeach()
    string(APPEND _code ")")
    cmake_language(EVAL CODE "${_code}")
endfunction()
