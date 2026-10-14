# Part of the parent_project fixture: an intermediate directory that finds
# Threads (creating a directory-scoped Threads::Threads) before adding mapc.
find_package(Threads REQUIRED)
add_subdirectory("${MAPC_SOURCE_DIR}" mapc)
