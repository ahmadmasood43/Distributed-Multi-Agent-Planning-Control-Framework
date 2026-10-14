# Provides mapc_enable_clang_tidy(<target>) which runs clang-tidy on a
# first-party target while it compiles. Configuration lives in .clang-tidy.

if(MAPC_ENABLE_CLANG_TIDY)
    find_program(MAPC_CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-18 clang-tidy-17 REQUIRED)
    message(STATUS "mapc: clang-tidy enabled (${MAPC_CLANG_TIDY_EXE})")
endif()

function(mapc_enable_clang_tidy target)
    if(MAPC_ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES
            CXX_CLANG_TIDY "${MAPC_CLANG_TIDY_EXE};--warnings-as-errors=*;--extra-arg=-Wno-unknown-warning-option")
    endif()
endfunction()
