#include "mapc/common/logging.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mapc {
namespace {

/// Captures records into a vector and restores the logger state afterwards.
class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_level_ = Logger::instance().level();
        Logger::instance().setLevel(LogLevel::Trace);
        Logger::instance().setSink([this](const LogRecord& record) {
            const std::lock_guard<std::mutex> lock(mutex_);
            records_.push_back(record);
        });
    }

    void TearDown() override {
        Logger::instance().setSink(nullptr);
        Logger::instance().setLevel(previous_level_);
    }

    std::vector<LogRecord> records() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }

private:
    LogLevel previous_level_ = LogLevel::Info;
    std::mutex mutex_;
    std::vector<LogRecord> records_;
};

// --- Level names -------------------------------------------------------------

TEST(LogLevelTest, ToStringNamesEveryLevel) {
    EXPECT_EQ(toString(LogLevel::Trace), "TRACE");
    EXPECT_EQ(toString(LogLevel::Debug), "DEBUG");
    EXPECT_EQ(toString(LogLevel::Info), "INFO");
    EXPECT_EQ(toString(LogLevel::Warn), "WARN");
    EXPECT_EQ(toString(LogLevel::Error), "ERROR");
    EXPECT_EQ(toString(LogLevel::Off), "OFF");
}

TEST(LogLevelTest, ParseIsCaseInsensitiveAndRoundTrips) {
    for (const auto level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn,
                             LogLevel::Error, LogLevel::Off}) {
        EXPECT_EQ(parseLogLevel(toString(level)), level);
    }
    EXPECT_EQ(parseLogLevel("debug"), LogLevel::Debug);
    EXPECT_EQ(parseLogLevel("Info"), LogLevel::Info);
    EXPECT_EQ(parseLogLevel("warning"), LogLevel::Warn);
    EXPECT_EQ(parseLogLevel("WARNING"), LogLevel::Warn);
}

TEST(LogLevelTest, OutOfRangeValueHasPlaceholderName) {
    EXPECT_EQ(toString(static_cast<LogLevel>(200)), "UNKNOWN");
}

TEST(LogLevelTest, ParseRejectsUnknownText) {
    EXPECT_FALSE(parseLogLevel("").has_value());
    EXPECT_FALSE(parseLogLevel("verbose").has_value());
    EXPECT_FALSE(parseLogLevel("info ").has_value());
    EXPECT_FALSE(parseLogLevel("inf").has_value());
    EXPECT_FALSE(parseLogLevel("\xFF\xFE").has_value());
}

// --- Formatting --------------------------------------------------------------

TEST(LogFormatTest, UsesBaseNameOfFile) {
    const LogRecord record{LogLevel::Warn, "/a/b/planner.cpp", 17, "slow plan"};
    EXPECT_EQ(formatLogRecord(record), "[WARN] planner.cpp:17 slow plan");
}

TEST(LogFormatTest, HandlesWindowsSeparatorsAndBareNames) {
    EXPECT_EQ(formatLogRecord({LogLevel::Info, "C:\\src\\x.cpp", 1, "m"}), "[INFO] x.cpp:1 m");
    EXPECT_EQ(formatLogRecord({LogLevel::Info, "x.cpp", 2, "m"}), "[INFO] x.cpp:2 m");
}

TEST(LogFormatTest, OmitsLocationWhenFileIsEmpty) {
    EXPECT_EQ(formatLogRecord({LogLevel::Error, "", 0, "boom"}), "[ERROR] boom");
}

TEST(LogFormatTest, EmptyMessageAndTrailingSlash) {
    EXPECT_EQ(formatLogRecord({LogLevel::Debug, "dir/", 3, ""}), "[DEBUG] :3 ");
}

// --- Logger behaviour --------------------------------------------------------

TEST_F(LoggingTest, MacroCapturesLevelLocationAndMessage) {
    const int line = __LINE__ + 1;
    MAPC_LOG_INFO << "agent " << 3 << " ready";
    const auto got = records();
    ASSERT_EQ(got.size(), 1U);
    EXPECT_EQ(got[0].level, LogLevel::Info);
    EXPECT_EQ(got[0].message, "agent 3 ready");
    EXPECT_EQ(got[0].line, line);
    EXPECT_NE(got[0].file.find("logging_test.cpp"), std::string_view::npos);
}

TEST_F(LoggingTest, EveryMacroLevelIsDelivered) {
    MAPC_LOG_TRACE << "t";
    MAPC_LOG_DEBUG << "d";
    MAPC_LOG_INFO << "i";
    MAPC_LOG_WARN << "w";
    MAPC_LOG_ERROR << "e";
    const auto got = records();
    ASSERT_EQ(got.size(), 5U);
    EXPECT_EQ(got[0].level, LogLevel::Trace);
    EXPECT_EQ(got[4].level, LogLevel::Error);
}

TEST_F(LoggingTest, RecordsBelowThresholdAreDropped) {
    Logger::instance().setLevel(LogLevel::Warn);
    MAPC_LOG_DEBUG << "hidden";
    MAPC_LOG_INFO << "hidden";
    MAPC_LOG_WARN << "shown";
    MAPC_LOG_ERROR << "shown";
    EXPECT_EQ(records().size(), 2U);
}

TEST_F(LoggingTest, DisabledStatementDoesNotEvaluateOperands) {
    Logger::instance().setLevel(LogLevel::Error);
    int evaluations = 0;
    const auto expensive = [&] {
        ++evaluations;
        return "value";
    };
    MAPC_LOG_DEBUG << expensive();
    EXPECT_EQ(evaluations, 0);
    MAPC_LOG_ERROR << expensive();
    EXPECT_EQ(evaluations, 1);
}

TEST_F(LoggingTest, OffSilencesEverythingIncludingErrors) {
    Logger::instance().setLevel(LogLevel::Off);
    MAPC_LOG_ERROR << "nope";
    EXPECT_TRUE(records().empty());
    EXPECT_FALSE(Logger::instance().enabled(LogLevel::Error));
}

TEST_F(LoggingTest, OffIsNeverAMessageLevel) {
    // Even with the most verbose threshold, a record tagged Off is not emitted.
    MAPC_LOG(LogLevel::Off) << "sentinel";
    EXPECT_TRUE(records().empty());
}

TEST_F(LoggingTest, MacroIsASingleStatementInUnbracedIfElse) {
    bool else_taken = false;
    const bool condition = false;
    if (condition)
        MAPC_LOG_INFO << "then";
    else
        else_taken = true;
    EXPECT_TRUE(else_taken);
    EXPECT_TRUE(records().empty());
}

TEST_F(LoggingTest, DirectWriteRespectsLevel) {
    Logger::instance().setLevel(LogLevel::Info);
    Logger::instance().write({LogLevel::Debug, "", 0, "below"});
    Logger::instance().write({LogLevel::Info, "", 0, "at"});
    const auto got = records();
    ASSERT_EQ(got.size(), 1U);
    EXPECT_EQ(got[0].message, "at");
}

TEST_F(LoggingTest, ThrowingSinkIsContainedAndCounted) {
    Logger::instance().setSink([](const LogRecord&) { throw std::runtime_error("sink failed"); });
    const auto before = Logger::instance().droppedCount();
    EXPECT_NO_THROW({ MAPC_LOG_ERROR << "lost"; });
    EXPECT_EQ(Logger::instance().droppedCount(), before + 1);

    // The logger stays usable afterwards.
    std::vector<std::string> seen;
    Logger::instance().setSink([&](const LogRecord& r) { seen.push_back(r.message); });
    MAPC_LOG_ERROR << "after";
    EXPECT_EQ(seen, std::vector<std::string>{"after"});
}

struct ThrowsWhenStreamed {};
std::ostream& operator<<(std::ostream& os, ThrowsWhenStreamed /*unused*/) {
    throw std::runtime_error("cannot format");
    return os;
}

TEST_F(LoggingTest, ThrowingOperandPropagatesAndDropsPartialMessage) {
    const auto before = Logger::instance().droppedCount();
    EXPECT_THROW(
        { MAPC_LOG_INFO << "partial " << ThrowsWhenStreamed{} << " never"; }, std::runtime_error);
    EXPECT_TRUE(records().empty()) << "a half-built message must not be emitted";
    EXPECT_EQ(Logger::instance().droppedCount(), before + 1);
}

TEST_F(LoggingTest, LoggingInsideCatchBlockStillWorks) {
    // An exception that is already in flight when the statement starts must not
    // suppress the record.
    try {
        throw std::runtime_error("outer");
    } catch (const std::exception& e) {
        MAPC_LOG_ERROR << "caught " << e.what();
    }
    const auto got = records();
    ASSERT_EQ(got.size(), 1U);
    EXPECT_EQ(got[0].message, "caught outer");
}

struct LogsInDestructor {
    LogsInDestructor() = default;
    LogsInDestructor(const LogsInDestructor&) = delete;
    LogsInDestructor& operator=(const LogsInDestructor&) = delete;
    LogsInDestructor(LogsInDestructor&&) = delete;
    LogsInDestructor& operator=(LogsInDestructor&&) = delete;
    ~LogsInDestructor() { MAPC_LOG_WARN << "cleanup during unwinding"; }
};

TEST_F(LoggingTest, LoggingFromDestructorDuringUnwindingIsDelivered) {
    // The statement starts and finishes while one exception is in flight; it is
    // complete, so it must be emitted.
    try {
        const LogsInDestructor guard;
        throw std::runtime_error("unwind");
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "unwind");
    }
    const auto got = records();
    ASSERT_EQ(got.size(), 1U);
    EXPECT_EQ(got[0].message, "cleanup during unwinding");
}

TEST_F(LoggingTest, ReentrantLoggingFromSinkIsDroppedNotDeadlocked) {
    std::vector<std::string> seen;
    Logger::instance().setSink([&](const LogRecord& r) {
        seen.push_back(r.message);
        MAPC_LOG_ERROR << "nested";  // would self-deadlock without the guard
    });
    const auto before = Logger::instance().droppedCount();
    MAPC_LOG_ERROR << "outer";
    EXPECT_EQ(seen, std::vector<std::string>{"outer"});
    EXPECT_EQ(Logger::instance().droppedCount(), before + 1);

    MAPC_LOG_ERROR << "second";  // guard was reset after the first delivery
    EXPECT_EQ(seen.size(), 2U);
}

TEST_F(LoggingTest, EmptySinkRestoresDefaultStderrSink) {
    Logger::instance().setSink(nullptr);
    testing::internal::CaptureStderr();
    MAPC_LOG_WARN << "to stderr";
    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_NE(output.find("[WARN] logging_test.cpp:"), std::string::npos);
    EXPECT_NE(output.find("to stderr\n"), std::string::npos);
}

TEST_F(LoggingTest, ConcurrentLoggingDeliversEveryRecordIntact) {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i) {
                MAPC_LOG_INFO << "thread=" << t << " i=" << i;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto got = records();
    ASSERT_EQ(got.size(), static_cast<std::size_t>(kThreads * kPerThread));
    for (const auto& r : got) {
        ASSERT_EQ(r.message.rfind("thread=", 0), 0U) << "torn record: " << r.message;
    }
}

/// Runs `body` repeatedly on a background thread while the caller performs
/// `rounds` mutations, each of which waits for the writer to make progress so
/// that writer and mutator are guaranteed to interleave.
template <typename Body, typename Mutation>
long runInterleaved(int rounds, Body body, Mutation mutation) {
    std::atomic<bool> stop{false};
    std::atomic<long> iterations{0};
    std::thread writer([&] {
        while (!stop.load()) {
            body();
            iterations.fetch_add(1);
        }
    });
    for (int round = 0; round < rounds; ++round) {
        const long seen = iterations.load();
        while (iterations.load() == seen) {
            std::this_thread::yield();
        }
        mutation(round);
    }
    stop.store(true);
    writer.join();
    return iterations.load();
}

TEST_F(LoggingTest, LevelCanChangeWhileOtherThreadsLog) {
    std::atomic<int> delivered{0};
    Logger::instance().setSink([&](const LogRecord&) { delivered.fetch_add(1); });
    const long iterations = runInterleaved(
        200, [] { MAPC_LOG_DEBUG << "spam"; },
        [](int round) {
            Logger::instance().setLevel(round % 2 == 0 ? LogLevel::Error : LogLevel::Trace);
        });
    EXPECT_GE(iterations, 200);
    EXPECT_GT(delivered.load(), 0) << "the writer never ran with Debug enabled";
}

TEST_F(LoggingTest, SinkCanBeReplacedWhileOtherThreadsLog) {
    std::atomic<int> delivered{0};
    const long iterations = runInterleaved(
        200, [] { MAPC_LOG_INFO << "x"; },
        [&](int /*round*/) {
            Logger::instance().setSink([&](const LogRecord&) { delivered.fetch_add(1); });
        });
    EXPECT_GE(iterations, 200);
    EXPECT_GT(delivered.load(), 0);
}

TEST_F(LoggingTest, SinkMayReplaceItselfWithoutDeadlock) {
    std::vector<std::string> second;
    Logger::instance().setSink([&](const LogRecord& first_record) {
        // Replacing the sink from inside a delivery must not self-deadlock, and
        // the running sink must stay alive until it returns.
        Logger::instance().setSink([&](const LogRecord& r) { second.push_back(r.message); });
        EXPECT_EQ(first_record.message, "one");
    });
    MAPC_LOG_INFO << "one";
    MAPC_LOG_INFO << "two";
    EXPECT_EQ(second, std::vector<std::string>{"two"});
}

/// Logs from its destructor; owned by a sink to model resources such as files
/// that report on close.
struct LogsWhenDestroyed {
    LogsWhenDestroyed() = default;
    LogsWhenDestroyed(const LogsWhenDestroyed&) = delete;
    LogsWhenDestroyed& operator=(const LogsWhenDestroyed&) = delete;
    LogsWhenDestroyed(LogsWhenDestroyed&&) = delete;
    LogsWhenDestroyed& operator=(LogsWhenDestroyed&&) = delete;
    ~LogsWhenDestroyed() { MAPC_LOG_INFO << "old sink closed"; }
};

TEST_F(LoggingTest, RetiredSinkMayLogFromItsDestructor) {
    std::vector<std::string> seen;
    {
        auto resource = std::make_shared<LogsWhenDestroyed>();
        Logger::instance().setSink([resource](const LogRecord&) {});
    }  // the sink now holds the only reference
    Logger::instance().setSink([&](const LogRecord& r) { seen.push_back(r.message); });
    EXPECT_EQ(seen, std::vector<std::string>{"old sink closed"});
}

TEST_F(LoggingTest, LevelExpressionIsEvaluatedExactlyOnce) {
    int calls = 0;
    const auto next_level = [&] {
        ++calls;
        return calls == 1 ? LogLevel::Info : LogLevel::Error;
    };
    MAPC_LOG(next_level()) << "once";
    EXPECT_EQ(calls, 1);
    const auto got = records();
    ASSERT_EQ(got.size(), 1U);
    EXPECT_EQ(got[0].level, LogLevel::Info) << "filtered and recorded level must agree";

    Logger::instance().setLevel(LogLevel::Error);
    calls = 0;
    MAPC_LOG(next_level()) << "filtered out";
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(records().size(), 1U);
}

}  // namespace
}  // namespace mapc
