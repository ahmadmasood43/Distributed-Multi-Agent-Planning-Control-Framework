# Builds tests/cmake/fixtures/header_deps_e2e with Ninja and checks that
#  - tools/check_header_deps.cmake, given only the target name, reports every
#    stub ROS header that went into it: through obfuscated directives in an
#    object library's objects, and through a symlink with an innocent name; and
#    ROS used in core-owned files it read (a generated header, an .ipp file);
#  - tools/check_core_symbols.cmake reports ROS functions and variables that
#    were declared by hand instead of included, run-time loading, and a ROS
#    library that a shared library needs only through DT_NEEDED.
#
#   cmake -DMAPC_SOURCE_DIR=<repo> -DWORK_DIR=<scratch> -DNINJA=<ninja>
#         -DCXX=<compiler> -DNM=<nm> -P tests/cmake/run_header_deps_e2e.cmake

cmake_minimum_required(VERSION 3.21)

set(_fixture "${MAPC_SOURCE_DIR}/tests/cmake/fixtures/header_deps_e2e")
file(REMOVE_RECURSE "${WORK_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_fixture}" -B "${WORK_DIR}" -G Ninja
            "-DCMAKE_MAKE_PROGRAM=${NINJA}" "-DCMAKE_CXX_COMPILER=${CXX}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Configuring the fixture failed:\n${_out}${_err}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Building the fixture failed:\n${_out}${_err}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${WORK_DIR}" "-DNINJA=${NINJA}"
            -DTARGETS=core_like
            "-DOWNED_ROOTS=${_fixture}/owned|${WORK_DIR}/generated"
            -P "${MAPC_SOURCE_DIR}/tools/check_header_deps.cmake"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
set(_output "${_out}${_err}")

if(_rc EQUAL 0)
    message(FATAL_ERROR "check_header_deps.cmake accepted ROS headers:\n${_output}")
endif()
# Obfuscated includes in the object library's objects...
foreach(_header a b c d)
    if(NOT _output MATCHES "component\\.dir/obfuscated\\.cpp\\.o: [^\n]*fake_ros/include/rclcpp/${_header}\\.hpp")
        message(FATAL_ERROR "rclcpp/${_header}.hpp (object library) was not reported:\n${_output}")
    endif()
endforeach()
# ...ROS used in core-owned files the compiler read: a generated header and an
# implementation header with a non-standard extension...
foreach(_owned "generated/mapc/generated_probe\\.hpp:2: uses ROS identifier 'rcl_init'"
               "generated/mapc/generated_probe\\.hpp:3: uses ROS identifier 'rcl_init'"
               "owned/impl/probe\\.ipp:4: uses ROS identifier 'namespace rclcpp'"
               "owned/impl/probe\\.ipp:8: uses ROS identifier 'rclcpp::'")
    if(NOT _output MATCHES "${_owned}")
        message(FATAL_ERROR "'${_owned}' was not reported:\n${_output}")
    endif()
endforeach()
# ...and the symlinked alias in the core library's own object.
if(NOT _output MATCHES "core_like\\.dir/core\\.cpp\\.o: [^\n]*alias_include/mapc/alias\\.hpp -> [^\n]*fake_ros/include/rclcpp/e\\.hpp")
    message(FATAL_ERROR "The symlinked ROS header was not reported:\n${_output}")
endif()

# The symbol check must catch ROS functions declared by hand (symbols.cpp).
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=${WORK_DIR}/libcore_like.a" "-DNM=${NM}"
            -P "${MAPC_SOURCE_DIR}/tools/check_core_symbols.cmake"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
set(_output "${_out}${_err}")
if(_rc EQUAL 0)
    message(FATAL_ERROR "check_core_symbols.cmake accepted ROS symbols:\n${_output}")
endif()
foreach(_symbol "symbols\\.cpp\\.o: rcl_init" "symbols\\.cpp\\.o: std_msgs::msg::String::String\\(\\)"
                "symbols\\.cpp\\.o: g_rcutils_logging_initialized"
                "symbols\\.cpp\\.o: std_msgs__msg__String__init"
                "symbols\\.cpp\\.o: dlopen \\(run-time code loading")
    if(NOT _output MATCHES "${_symbol}")
        message(FATAL_ERROR "Symbol '${_symbol}' was not reported:\n${_output}")
    endif()
endforeach()

# A shared library whose only ROS dependency is a DT_NEEDED entry.
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=${WORK_DIR}/libcore_shared.so" "-DNM=${NM}"
            -P "${MAPC_SOURCE_DIR}/tools/check_core_symbols.cmake"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
set(_output "${_out}${_err}")
if(_rc EQUAL 0 OR NOT _output MATCHES "DT_NEEDED librclcpp\\.so is not a toolchain runtime library")
    message(FATAL_ERROR "The DT_NEEDED dependency on librclcpp.so was not reported:\n${_output}")
endif()

message(STATUS "All obfuscated, object-library, symlinked, hand-declared and DT_NEEDED ROS dependencies were detected")
