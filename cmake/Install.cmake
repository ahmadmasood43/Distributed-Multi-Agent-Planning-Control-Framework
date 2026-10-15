# Install rules and a relocatable package config, so downstream projects
# (for example the ROS 2 adapter) can use find_package(mapc).

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS mapc_core
    EXPORT mapcTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# mapc_io is not installed yet, so neither are its headers.
install(DIRECTORY "${PROJECT_SOURCE_DIR}/core/include/mapc"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
    PATTERN "io" EXCLUDE)

install(FILES "${PROJECT_BINARY_DIR}/generated/include/mapc/version.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mapc")

set(MAPC_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/mapc")

install(EXPORT mapcTargets
    NAMESPACE mapc::
    DESTINATION ${MAPC_INSTALL_CMAKEDIR})

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/mapcConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/mapcConfig.cmake"
    INSTALL_DESTINATION ${MAPC_INSTALL_CMAKEDIR})

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/mapcConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

install(FILES
    "${PROJECT_BINARY_DIR}/mapcConfig.cmake"
    "${PROJECT_BINARY_DIR}/mapcConfigVersion.cmake"
    DESTINATION ${MAPC_INSTALL_CMAKEDIR})
