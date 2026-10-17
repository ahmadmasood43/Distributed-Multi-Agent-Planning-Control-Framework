# Command-line tools. Their logic lives in small libraries so the unit tests
# can drive it directly.

add_library(mapc_plan_cli STATIC mapc_plan/plan_cli.cpp)
target_include_directories(mapc_plan_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/mapc_plan")
target_link_libraries(mapc_plan_cli
    PUBLIC mapc::core mapc::io
    PRIVATE mapc::warnings)
mapc_enable_clang_tidy(mapc_plan_cli)

add_executable(mapc_plan mapc_plan/main.cpp)
target_link_libraries(mapc_plan PRIVATE mapc_plan_cli mapc::warnings)
mapc_enable_clang_tidy(mapc_plan)

add_library(mapc_bt_demo_lib STATIC mapc_bt_demo/toy_agent.cpp)
target_include_directories(mapc_bt_demo_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/mapc_bt_demo")
target_link_libraries(mapc_bt_demo_lib
    PUBLIC mapc::core
    PRIVATE mapc::warnings)
mapc_enable_clang_tidy(mapc_bt_demo_lib)

add_executable(mapc_bt_demo mapc_bt_demo/main.cpp)
target_link_libraries(mapc_bt_demo PRIVATE mapc_bt_demo_lib mapc::warnings)
mapc_enable_clang_tidy(mapc_bt_demo)
