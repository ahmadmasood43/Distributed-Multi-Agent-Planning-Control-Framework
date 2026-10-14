# Part of the dependency_rules fixture: scoped::dep is an imported target local
# to this directory, so the parent directory cannot see it.
add_library(scoped::dep INTERFACE IMPORTED)
add_library(scoped_user INTERFACE)
target_link_libraries(scoped_user INTERFACE scoped::dep)
