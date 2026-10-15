#pragma once

#include <cstddef>
#include <string>

namespace mapc {

/// A problem found while reading a map or graph file.
struct LoadError {
    /// 1-based line of the problem, or 0 if it is not tied to a line (for
    /// example, a file that cannot be opened).
    std::size_t line = 0;
    std::string message;

    /// "line 12: message", or just the message when line is 0.
    [[nodiscard]] std::string describe() const {
        return line == 0 ? message : "line " + std::to_string(line) + ": " + message;
    }
};

}  // namespace mapc
