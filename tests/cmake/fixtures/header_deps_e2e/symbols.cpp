// Core-like translation unit for architecture.compiled_checks_detect_real_dependencies.
// It uses ROS without including any ROS header: functions and variables are
// declared by hand and the link is left to whoever links the library, or the
// library is loaded by name at run time.
extern "C" int rcl_init(int argc, const char* const* argv, void* options, void* context);
extern "C" bool g_rcutils_logging_initialized;
extern "C" bool std_msgs__msg__String__init(void* message);
extern "C" void* dlopen(const char* file, int mode);

namespace std_msgs::msg {
struct String {
    String();
};
}  // namespace std_msgs::msg

int fixtureCallsRos() {
    const std_msgs::msg::String message;
    (void)message;
    (void)dlopen("librcl.so", 1);
    (void)std_msgs__msg__String__init(nullptr);
    return g_rcutils_logging_initialized ? rcl_init(0, nullptr, nullptr, nullptr) : 0;
}
