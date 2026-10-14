// Core-owned translation unit for architecture.compiled_checks_detect_real_dependencies.
// Both included files use ROS only in inline code that is never called, so no
// symbol is left behind; only scanning the files the compiler read finds it.
#include "mapc/generated_probe.hpp"

#include "impl/probe.ipp"

int fixtureOwned() {
    return 2;
}
