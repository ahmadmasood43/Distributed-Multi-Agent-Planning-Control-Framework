#include "mapc/common/logging.hpp"

#include <cctype>
#include <exception>
#include <iostream>
#include <utility>

namespace mapc {

namespace {

void defaultSink(const LogRecord& record) {
    // One insertion per record so concurrent processes' lines do not interleave.
    std::cerr << formatLogRecord(record) + '\n' << std::flush;
}

std::string_view baseName(std::string_view path) noexcept {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

// Set while the current thread is inside the sink, to detect re-entrant logging.
thread_local bool t_in_sink = false;

}  // namespace

std::string_view toString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Off:
            return "OFF";
    }
    return "UNKNOWN";
}

std::optional<LogLevel> parseLogLevel(std::string_view text) noexcept {
    for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn,
                                 LogLevel::Error, LogLevel::Off}) {
        if (equalsIgnoreCase(text, toString(level))) {
            return level;
        }
    }
    if (equalsIgnoreCase(text, "warning")) {
        return LogLevel::Warn;
    }
    return std::nullopt;
}

std::string formatLogRecord(const LogRecord& record) {
    std::string out;
    out.reserve(record.message.size() + 48);
    out += '[';
    out += toString(record.level);
    out += "] ";
    if (!record.file.empty()) {
        out += baseName(record.file);
        out += ':';
        out += std::to_string(record.line);
        out += ' ';
    }
    out += record.message;
    return out;
}

Logger::Logger() : level_(LogLevel::Info), sink_(std::make_shared<const LogSink>(defaultSink)) {}

Logger& Logger::instance() {
    // Intentionally leaked so logging from static destructors stays valid.
    static auto* logger = new Logger();  // NOLINT(cppcoreguidelines-owning-memory)
    return *logger;
}

void Logger::setLevel(LogLevel level) noexcept {
    level_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() const noexcept {
    return level_.load(std::memory_order_relaxed);
}

bool Logger::enabled(LogLevel level) const noexcept {
    return level != LogLevel::Off && level >= this->level();
}

std::shared_ptr<const LogSink> Logger::currentSink() const {
    const std::lock_guard<std::mutex> lock(sink_mutex_);
    return sink_;
}

void Logger::setSink(LogSink sink) {
    auto replacement =
        std::make_shared<const LogSink>(sink ? std::move(sink) : LogSink(defaultSink));
    {
        const std::lock_guard<std::mutex> lock(sink_mutex_);
        sink_.swap(replacement);
    }
    // `replacement` now holds the retired sink. Unless a delivery still uses it,
    // it is destroyed here, after the lock is released, so a destructor that
    // logs cannot deadlock.
}

void Logger::write(const LogRecord& record) noexcept {
    if (!enabled(record.level)) {
        return;
    }
    if (t_in_sink) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    try {
        // Declared before the lock so that, if this is the last reference to a
        // retired sink, the sink is destroyed after the lock is released.
        const std::shared_ptr<const LogSink> sink = currentSink();
        const std::lock_guard<std::mutex> lock(delivery_mutex_);
        t_in_sink = true;
        try {
            (*sink)(record);
        } catch (...) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        t_in_sink = false;
    } catch (...) {
        // std::mutex::lock can throw std::system_error.
        t_in_sink = false;
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t Logger::droppedCount() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
}

namespace detail {

LogLine::LogLine(LogLevel level, const char* file, int line)
    : level_(level), file_(file), line_(line), uncaught_on_entry_(std::uncaught_exceptions()) {}

LogLine::~LogLine() {
    Logger& logger = Logger::instance();
    if (std::uncaught_exceptions() > uncaught_on_entry_) {
        // Streaming threw: the message is incomplete, so do not emit it.
        logger.dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    try {
        logger.write(LogRecord{level_, file_ != nullptr ? file_ : "", line_, stream_.str()});
    } catch (...) {
        // Building the record (stream_.str()) can throw std::bad_alloc; a log
        // statement must never terminate the program from a destructor.
        logger.dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace detail

}  // namespace mapc
