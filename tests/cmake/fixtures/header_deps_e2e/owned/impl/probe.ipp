// Implementation header with a non-standard extension.
#pragma once

namespace rclcpp {
struct Node;
}  // namespace rclcpp

inline int ippProbe(rclcpp::Node* node) { return node != nullptr ? 1 : 0; }
