# Follow-ups

Hardening ideas and known limitations that do not block the milestone they
were found in. Each entry says where it came from and when it should be
picked up.

| # | From | Item | Pick up |
|---|---|---|---|
| 1 | M0 review | The installed-files architecture scan (`tests/cmake/run_installed_scan.cmake`) parses every installed non-binary file as C/C++. An installed Markdown guide that merely mentions `rcl_init` would be reported as a ROS dependency. Scan an explicit header inventory as C++ and treat documentation separately. | Before any documentation is installed. |
| 2 | M1 | `mapc_io` (the YAML loader) is not installed or exported. Installing it needs yaml-cpp in the export set or a `find_dependency(yaml-cpp)` in the package config. | When a downstream consumer needs the YAML loader. |
| 3 | M1 | yaml-cpp 0.8.0 declares `cmake_minimum_required(VERSION 3.4)`. CMake 3.x prints a deprecation warning while configuring, and CMake ≥ 4 needs the `CMAKE_POLICY_VERSION_MINIMUM` workaround in `core/CMakeLists.txt`. Drop both when a yaml-cpp release with a newer minimum is available. | Next yaml-cpp release. |
| 4 | M1 review | `WeightedAStarPlannerTest.LargeHeuristicTimesLargeNumeratorIsExact` also passes against the old saturating priorities, so it does not pin the numerator × h overflow on its own (the denominator-overflow regression and the `UInt128` cross-check against native 128-bit arithmetic do). Rework it into a case where saturating `numerator × h` changes the expansion order. | Next time the planner priorities are touched. |
