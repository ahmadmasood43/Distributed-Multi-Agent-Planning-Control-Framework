# C/C++ source scanner shared by the repository checks in this directory.
#
#   mapc_cpp_scan(<content> <out_var>)
#
# Sets
#   <out_var>_INCLUDES     "<line>|<header-name>" for every #include directive
#   <out_var>_IDENTIFIERS  "<line>|<text>" for every ROS identifier used in code
#
# The lexer models the translation phases that matter: CRLF normalisation,
# backslash-newline splicing (phase 2), comments, string/character/raw-string
# literals, digit separators (phase 3), and the `%:` digraph. Line numbers refer
# to the original physical lines.
#
# Identifiers are matched on a "code-only" copy of the text in which comments
# and literals are blanked (newlines kept, so positions are unchanged). Tokens
# separated by whitespace, newlines or comments therefore still form one match:
# `rclcpp /* x */ :: get_logger` and `namespace\n rclcpp` are both found.
#
# ROS identifiers are: ROS C API names (rcl_init, g_rcutils_...), rosidl C names
# (std_msgs__msg__...), qualified names in a ROS C++ namespace (rclcpp::...,
# ::std_msgs::...), and namespace declarations, using-directives and aliases
# naming one. A name nested in another namespace (mapc::agent_msgs::Status) is
# not a ROS name: ROS packages are top-level namespaces.

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ros_packages.cmake")

string(ASCII 11 _MAPC_VT)
string(ASCII 12 _MAPC_FF)

# Physical line of offset `at` in the spliced text (newlines before it plus the
# splices removed before it).
function(_mapc_cpp_line text splices at out_var)
    string(SUBSTRING "${text}" 0 ${at} _upto)
    string(REGEX MATCHALL "\n" _nl "${_upto}")
    list(LENGTH _nl _line)
    foreach(_s IN LISTS splices)
        if(_s LESS_EQUAL at)
            math(EXPR _line "${_line} + 1")
        endif()
    endforeach()
    math(EXPR _line "${_line} + 1")
    set(${out_var} ${_line} PARENT_SCOPE)
endfunction()

# Appends `text` to `out_var` with every character except newlines replaced by
# a space, so that positions and line numbers are preserved.
function(_mapc_blank text out_var)
    string(REGEX REPLACE "[^\n]" " " _blank "${text}")
    set(${out_var} "${${out_var}}${_blank}" PARENT_SCOPE)
endfunction()

# Position and character of the last non-whitespace character before `pos`
# in `code` (-1 and "" if there is none).
function(_mapc_prev_significant code pos out_pos out_char)
    set(_p ${pos})
    set(_ch "")
    while(_p GREATER 0)
        math(EXPR _p "${_p} - 1")
        string(SUBSTRING "${code}" ${_p} 1 _ch)
        if(NOT _ch MATCHES "^[ \t\n\r${_MAPC_VT}${_MAPC_FF}]$")
            set(${out_pos} ${_p} PARENT_SCOPE)
            set(${out_char} "${_ch}" PARENT_SCOPE)
            return()
        endif()
    endwhile()
    set(${out_pos} -1 PARENT_SCOPE)
    set(${out_char} "" PARENT_SCOPE)
endfunction()

function(mapc_cpp_scan content out_var)
    set(_hs "[ \t${_MAPC_VT}${_MAPC_FF}]")
    # Whitespace or block comment: a comment is replaced by a space in
    # translation phase 3, so it may sit between the tokens of a directive.
    set(_sp "(${_hs}|/\\*([^*]|\\*+[^*/])*\\*+/)*")
    set(_directive_regex "^${_sp}(#|%:)${_sp}include${_sp}(<[^>\n]*>|\"[^\"\n]*\")")

    string(REPLACE "\r\n" "\n" content "${content}")

    # Phase 2: splice lines, remembering where each splice happened.
    set(_text "")
    set(_splices "")
    set(_rest "${content}")
    set(_splicing 1)
    while(_splicing)
        string(FIND "${_rest}" "\\\n" _idx)
        if(_idx EQUAL -1)
            string(APPEND _text "${_rest}")
            set(_splicing 0)
            continue()
        endif()
        string(SUBSTRING "${_rest}" 0 ${_idx} _before)
        string(APPEND _text "${_before}")
        string(LENGTH "${_text}" _offset)
        list(APPEND _splices ${_offset})
        math(EXPR _idx "${_idx} + 2")
        string(SUBSTRING "${_rest}" ${_idx} -1 _rest)
    endwhile()

    # Phase 3: tokenise. Produces the include list and `_code`, a copy of
    # `_text` with comments, literals and directives blanked.
    set(_includes "")
    set(_code "")
    set(_rest "${_text}")
    set(_pos 0)
    set(_at_line_start TRUE)  # only whitespace/comments so far on this line
    set(_prev_chunk "")       # code run immediately before the current char
    set(_in_number FALSE)     # inside a pp-number containing digit separators

    while(NOT _rest STREQUAL "")
        if(_at_line_start AND _rest MATCHES "${_directive_regex}")
            set(_piece "${CMAKE_MATCH_0}")
            string(REGEX MATCH "(<[^>\n]*>|\"[^\"\n]*\")$" _name "${_piece}")
            string(REGEX MATCH "^[^#%]+" _lead "${_piece}")
            string(LENGTH "${_lead}" _lead_len)
            math(EXPR _at "${_pos} + ${_lead_len}")
            _mapc_cpp_line("${_text}" "${_splices}" ${_at} _line)
            list(APPEND _includes "${_line}|${_name}")
            string(LENGTH "${_piece}" _n)
            _mapc_blank("${_piece}" _code)
            set(_at_line_start FALSE)
            set(_prev_chunk "")
            set(_in_number FALSE)
        else()
            string(SUBSTRING "${_rest}" 0 1 _c)
            string(SUBSTRING "${_rest}" 0 2 _c2)
            set(_next_chunk "")
            set(_next_in_number FALSE)
            set(_n 1)

            if(_c STREQUAL "\n")
                string(APPEND _code "\n")
                set(_at_line_start TRUE)
            elseif(_c2 STREQUAL "//")
                string(FIND "${_rest}" "\n" _n)
                if(_n EQUAL -1)
                    string(LENGTH "${_rest}" _n)
                endif()
                string(SUBSTRING "${_rest}" 0 ${_n} _piece)
                _mapc_blank("${_piece}" _code)
            elseif(_c2 STREQUAL "/*")
                # A comment acts as a single space and does not end the line.
                string(SUBSTRING "${_rest}" 2 -1 _after)
                string(FIND "${_after}" "*/" _n)
                if(_n EQUAL -1)
                    string(LENGTH "${_rest}" _n)
                else()
                    math(EXPR _n "${_n} + 4")
                endif()
                string(SUBSTRING "${_rest}" 0 ${_n} _piece)
                _mapc_blank("${_piece}" _code)
            elseif(_c STREQUAL "\"" AND _prev_chunk MATCHES "(^|[^A-Za-z0-9_])(u8|u|U|L)?R$"
                   AND _rest MATCHES "^\"([^ ()\\\t\n${_MAPC_VT}${_MAPC_FF}]*)\\(")
                # Raw string literal: the prefix must immediately precede the quote.
                set(_close ")${CMAKE_MATCH_1}\"")
                string(LENGTH "${CMAKE_MATCH_0}" _open_len)
                string(SUBSTRING "${_rest}" ${_open_len} -1 _after)
                string(FIND "${_after}" "${_close}" _end)
                if(_end EQUAL -1)
                    string(LENGTH "${_rest}" _n)
                else()
                    string(LENGTH "${_close}" _close_len)
                    math(EXPR _n "${_open_len} + ${_end} + ${_close_len}")
                endif()
                string(SUBSTRING "${_rest}" 0 ${_n} _piece)
                _mapc_blank("${_piece}" _code)
                set(_at_line_start FALSE)
            elseif(_c STREQUAL "'" AND (_prev_chunk MATCHES "(^|[^A-Za-z0-9_.])[.]?[0-9][0-9A-Za-z_.]*$"
                                        OR (_in_number AND _prev_chunk MATCHES "^[0-9A-Za-z_.]+$")))
                # C++14 digit separator inside a number such as 1'000 or 0x1'ff.
                string(APPEND _code " ")
                set(_next_in_number TRUE)
            elseif(_c STREQUAL "\"" OR _c STREQUAL "'")
                # String or character literal (any u8/u/U/L prefix is ordinary code).
                if(_c STREQUAL "\"")
                    set(_literal_regex "^\"([^\"\\\\\n]|\\\\.)*\"")
                else()
                    set(_literal_regex "^'([^'\\\\\n]|\\\\.)*'")
                endif()
                if(_rest MATCHES "${_literal_regex}")
                    string(LENGTH "${CMAKE_MATCH_0}" _n)
                endif()
                string(SUBSTRING "${_rest}" 0 ${_n} _piece)
                _mapc_blank("${_piece}" _code)
                set(_at_line_start FALSE)
            else()
                # A run of ordinary code up to the next interesting character.
                string(REGEX MATCH "^[^\n/\"']+" _chunk "${_rest}")
                if(_chunk STREQUAL "")
                    set(_chunk "${_c}")  # a lone '/'
                endif()
                string(LENGTH "${_chunk}" _n)
                string(APPEND _code "${_chunk}")
                if(_chunk MATCHES "[^ \t\r${_MAPC_VT}${_MAPC_FF}]")
                    set(_at_line_start FALSE)
                endif()
                set(_next_chunk "${_chunk}")
                set(_next_in_number "${_in_number}")
            endif()

            set(_prev_chunk "${_next_chunk}")
            set(_in_number "${_next_in_number}")
        endif()

        math(EXPR _pos "${_pos} + ${_n}")
        string(SUBSTRING "${_rest}" ${_n} -1 _rest)
    endwhile()

    # Identifier scan over the code-only text.
    set(_ws "[ \t\n\r${_MAPC_VT}${_MAPC_FF}]")
    set(_pkg "${MAPC_ROS_PACKAGE_REGEX}")
    set(_kinds C_API ROSIDL QUALIFIED NAMESPACE ALIAS)
    # C API functions/variables and rosidl C names.
    set(_regex_C_API "(^|[^A-Za-z0-9_])${MAPC_ROS_C_PREFIX_REGEX}[A-Za-z0-9_]*")
    set(_regex_ROSIDL "(^|[^A-Za-z0-9_])${_pkg}__[A-Za-z0-9_]*")
    # Qualified names in a ROS namespace (nested qualification filtered below).
    set(_regex_QUALIFIED "(^|[^A-Za-z0-9_])${_pkg}${_ws}*::")
    # namespace declarations and using-directives naming a ROS namespace.
    set(_regex_NAMESPACE "(^|[^A-Za-z0-9_])namespace${_ws}+(::${_ws}*)?${_pkg}([^A-Za-z0-9_]|$)")
    # namespace aliases: namespace r = rclcpp;
    set(_regex_ALIAS "(^|[^A-Za-z0-9_])namespace${_ws}+[A-Za-z_][A-Za-z0-9_]*${_ws}*=${_ws}*(::${_ws}*)?${_pkg}([^A-Za-z0-9_]|$)")

    set(_identifiers "")
    foreach(_kind IN LISTS _kinds)
        set(_regex "${_regex_${_kind}}")
        set(_scan "${_code}")
        set(_base 0)
        while(_scan MATCHES "${_regex}")
            set(_match "${CMAKE_MATCH_0}")
            string(FIND "${_scan}" "${_match}" _idx)
            string(LENGTH "${_match}" _len)
            string(REGEX MATCH "^[^A-Za-z_]+" _lead "${_match}")
            string(LENGTH "${_lead}" _lead_len)
            math(EXPR _start "${_base} + ${_idx} + ${_lead_len}")

            # A qualified name nested in another namespace (mapc::agent_msgs::)
            # is not a ROS name; a leading global qualifier (::std_msgs::) is.
            # Walk back over whitespace (comments are already blanked) to the
            # previous significant tokens: "<identifier or >> ::" means nested.
            set(_nested FALSE)
            if(_kind STREQUAL "QUALIFIED")
                _mapc_prev_significant("${_code}" ${_start} _p _ch)
                if(_ch STREQUAL ":" AND _p GREATER 0)
                    math(EXPR _p2 "${_p} - 1")
                    string(SUBSTRING "${_code}" ${_p2} 1 _ch2)
                    if(_ch2 STREQUAL ":")
                        _mapc_prev_significant("${_code}" ${_p2} _p3 _ch3)
                        if(_ch3 MATCHES "^[A-Za-z0-9_>]$")
                            set(_nested TRUE)
                        endif()
                    endif()
                endif()
            endif()

            if(NOT _nested)
                _mapc_cpp_line("${_text}" "${_splices}" ${_start} _line)
                string(SUBSTRING "${_match}" ${_lead_len} -1 _shown)
                string(REGEX REPLACE "${_ws}+" " " _shown "${_shown}")
                string(REGEX REPLACE "[^A-Za-z0-9_:]+$" "" _shown "${_shown}")
                string(REGEX REPLACE "([^:]):$" "\\1" _shown "${_shown}")
                list(APPEND _identifiers "${_line}|${_shown}")
            endif()

            math(EXPR _advance "${_idx} + ${_len}")
            if(_advance EQUAL 0)
                break()
            endif()
            math(EXPR _base "${_base} + ${_advance}")
            string(SUBSTRING "${_scan}" ${_advance} -1 _scan)
        endwhile()
    endforeach()

    set(${out_var}_INCLUDES "${_includes}" PARENT_SCOPE)
    set(${out_var}_IDENTIFIERS "${_identifiers}" PARENT_SCOPE)
endfunction()
