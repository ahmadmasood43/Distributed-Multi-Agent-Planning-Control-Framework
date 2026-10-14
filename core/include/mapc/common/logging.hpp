#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace mapc {

namespace detail {
class LogLine;
}  // namespace detail

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };

/// Upper-case level name ("TRACE" ... "OFF").
[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

/// Case-insensitive inverse of toString(); also accepts "warning".
[[nodiscard]] std::optional<LogLevel> parseLogLevel(std::string_view text) noexcept;

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string_view file;  // as given by __FILE__; valid for the program's lifetime
    int line = 0;
    std::string message;
};

/// "[LEVEL] file.cpp:42 message", with the file reduced to its base name.
[[nodiscard]] std::string formatLogRecord(const LogRecord& record);

using LogSink = std::function<void(const LogRecord&)>;

/// Process-wide logger with a runtime level threshold and a replaceable sink.
///
/// Thread safety: all members may be called concurrently. Records are passed to
/// the sink one at a time, so sinks need not be thread-safe themselves.
///
/// Failure handling:
/// - Exceptions thrown by a sink are swallowed and counted in droppedCount().
/// - A sink that logs re-entrantly has the nested record dropped and counted.
/// - A sink may call setSink() (including from inside the sink). The replaced
///   sink stays alive until in-flight deliveries finish and is destroyed
///   outside every logger lock, so its destructor may itself log.
/// - Exceptions thrown while *formatting* a MAPC_LOG statement (for example by
///   a user-defined operator<<) propagate to the caller like any other
///   expression; the incomplete record is dropped and counted.
class Logger {
public:
    [[nodiscard]] static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
    ~Logger() = default;

    void setLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel level() const noexcept;
    [[nodiscard]] bool enabled(LogLevel level) const noexcept;

    /// Replaces the sink. An empty function restores the default stderr sink.
    void setSink(LogSink sink);

    /// Delivers a record to the sink if its level is enabled.
    void write(const LogRecord& record) noexcept;

    /// Number of records dropped because the sink threw or logged re-entrantly.
    [[nodiscard]] std::uint64_t droppedCount() const noexcept;

private:
    friend class detail::LogLine;  // counts records lost while being built

    Logger();

    [[nodiscard]] std::shared_ptr<const LogSink> currentSink() const;

    std::atomic<LogLevel> level_;
    std::atomic<std::uint64_t> dropped_{0};
    mutable std::mutex sink_mutex_;  // guards sink_ (the pointer, not calls)
    std::mutex delivery_mutex_;      // serialises calls into the sink
    std::shared_ptr<const LogSink> sink_;
};

namespace detail {

/// Collects one streamed log message and hands it to the logger on destruction.
///
/// If an exception escapes while the message is being streamed (for example a
/// throwing operator<<), the partial message is dropped and counted rather
/// than emitted during stack unwinding.
class LogLine {
public:
    LogLine(LogLevel level, const char* file, int line);
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    LogLine(LogLine&&) = delete;
    LogLine& operator=(LogLine&&) = delete;
    ~LogLine();

    std::ostream& stream() noexcept { return stream_; }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    int uncaught_on_entry_;
    std::ostringstream stream_;
};

}  // namespace detail

}  // namespace mapc

// The nested for-statements make the macro a single statement (safe inside an
// unbraced if/else), evaluate `level` exactly once, and skip formatting
// entirely when the level is disabled.
#define MAPC_LOG(level)                                                             \
    for (bool mapc_log_once_ = true; mapc_log_once_; mapc_log_once_ = false)        \
        for (const ::mapc::LogLevel mapc_log_level_ = (level);                      \
             mapc_log_once_ && ::mapc::Logger::instance().enabled(mapc_log_level_); \
             mapc_log_once_ = false)                                                \
    ::mapc::detail::LogLine(mapc_log_level_, __FILE__, __LINE__).stream()

#define MAPC_LOG_TRACE MAPC_LOG(::mapc::LogLevel::Trace)
#define MAPC_LOG_DEBUG MAPC_LOG(::mapc::LogLevel::Debug)
#define MAPC_LOG_INFO MAPC_LOG(::mapc::LogLevel::Info)
#define MAPC_LOG_WARN MAPC_LOG(::mapc::LogLevel::Warn)
#define MAPC_LOG_ERROR MAPC_LOG(::mapc::LogLevel::Error)
