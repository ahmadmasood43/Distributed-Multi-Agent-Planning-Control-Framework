# Fixture for architecture.installed_header_scan_*: a project that generates
# public headers nothing compiles (no source-tree wrapper includes them) and
# installs them, one of them without a file extension. Only scanning the
# installed files can see them.
cmake_minimum_required(VERSION 3.21)
project(installed_headers LANGUAGES NONE)

include(GNUInstallDirs)

file(WRITE "${PROJECT_BINARY_DIR}/generated/include/mapc/generated_probe.hpp" [=[
#pragma once
extern "C" int rcl_init(int, const char* const*, const void*, void*);
namespace mapc {
inline int generatedProbe() { return rcl_init(0, nullptr, nullptr, nullptr); }
}  // namespace mapc
]=])
file(COPY_FILE "${PROJECT_BINARY_DIR}/generated/include/mapc/generated_probe.hpp"
     "${PROJECT_BINARY_DIR}/generated/include/mapc/generated_probe")
install(FILES
    "${PROJECT_BINARY_DIR}/generated/include/mapc/generated_probe.hpp"
    "${PROJECT_BINARY_DIR}/generated/include/mapc/generated_probe"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mapc")
