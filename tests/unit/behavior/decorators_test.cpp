#include "mapc/behavior/decorators.hpp"

#include "mapc/behavior/builder.hpp"

#include "../support/bt_fixtures.hpp"
#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace mapc::bt {
namespace {

using testing::EventLog;
using testing::probe;
using testing::ProbeNode;
using testing::Ticker;

constexpr Status kS = Status::Success;
constexpr Status kF = Status::Failure;
constexpr Status kR = Status::Running;

std::vector<Tick> ticksAt(std::initializer_list<Tick::rep> values) {
    std::vector<Tick> ticks;
    for (const Tick::rep value : values) {
        ticks.emplace_back(value);
    }
    return ticks;
}

TEST(DecoratorTest, RejectsANullChild) {
    EXPECT_THROW(Inverter(nullptr), std::invalid_argument);
    EXPECT_THROW(Retry(1, nullptr), std::invalid_argument);
    EXPECT_THROW(Timeout(Duration{1}, nullptr), std::invalid_argument);
    EXPECT_THROW(RateLimit(Duration{1}, nullptr), std::invalid_argument);
}

TEST(DecoratorTest, ExposesItsChild) {
    ProbeNode* p = nullptr;
    const NodePtr inv = inverter(probe("a", nullptr, p));
    ASSERT_EQ(inv->childCount(), 1U);
    EXPECT_EQ(&inv->child(0), p);
    EXPECT_THROW((void)inv->child(1), std::out_of_range);
}

TEST(DecoratorTest, TypeNamesShowParameters) {
    ProbeNode* p = nullptr;
    EXPECT_EQ(inverter(probe("a", nullptr, p))->typeName(), "Inverter");
    EXPECT_EQ(retry(3, probe("a", nullptr, p))->typeName(), "Retry(3)");
    EXPECT_EQ(timeout(Duration{50}, probe("a", nullptr, p))->typeName(), "Timeout(50)");
    EXPECT_EQ(rateLimit(Duration{5}, probe("a", nullptr, p))->typeName(), "RateLimit(5)");
}

// --- Inverter -----------------------------------------------------------------------

TEST(InverterTest, SwapsSuccessAndFailureAndPassesRunning) {
    ProbeNode* p = nullptr;
    const NodePtr inv = inverter(probe("a", nullptr, p));
    p->script({kS, kF, kR});
    Ticker t;
    EXPECT_EQ(t.tick(*inv, 0), kF);
    EXPECT_EQ(t.tick(*inv, 1), kS);
    EXPECT_EQ(t.tick(*inv, 2), kR);
}

TEST(InverterTest, HaltHaltsTheChild) {
    ProbeNode* p = nullptr;
    const NodePtr inv = inverter(probe("a", nullptr, p, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*inv, 0), kR);
    inv->halt();
    EXPECT_EQ(p->halts(), 1U);
    EXPECT_EQ(inv->state(), NodeState::Halted);
}

// --- Retry --------------------------------------------------------------------------

TEST(RetryTest, RejectsZeroAttempts) {
    ProbeNode* p = nullptr;
    EXPECT_THROW((void)retry(0, probe("a", nullptr, p)), std::invalid_argument);
}

TEST(RetryTest, PassesAFirstSuccessThrough) {
    ProbeNode* p = nullptr;
    const NodePtr r = retry(3, probe("a", nullptr, p));
    Ticker t;
    EXPECT_EQ(t.tick(*r, 0), kS);
    EXPECT_EQ(p->ticks(), 1U);
}

TEST(RetryTest, RetriesOnTheNextTickAfterAFailure) {
    ProbeNode* p = nullptr;
    const NodePtr r = retry(3, probe("a", nullptr, p));
    auto* node = dynamic_cast<Retry*>(r.get());
    p->script({kF, kF});
    Ticker t;
    EXPECT_EQ(t.tick(*r, 0), kR);  // attempt 1 failed
    EXPECT_EQ(node->failedAttempts(), 1U);
    EXPECT_EQ(t.tick(*r, 1), kR);  // attempt 2 failed
    EXPECT_EQ(t.tick(*r, 2), kS);  // attempt 3 succeeded
    EXPECT_EQ(p->tickTimes(), ticksAt({0, 1, 2}));
    EXPECT_EQ(node->failedAttempts(), 0U);
}

TEST(RetryTest, FailsAfterTheLastAttemptAndStartsOverAfterwards) {
    ProbeNode* p = nullptr;
    const NodePtr r = retry(2, probe("a", nullptr, p, kF));
    Ticker t;
    EXPECT_EQ(t.tick(*r, 0), kR);
    EXPECT_EQ(t.tick(*r, 1), kF);
    EXPECT_EQ(p->ticks(), 2U);
    // A new run gets a fresh set of attempts.
    EXPECT_EQ(t.tick(*r, 2), kR);
    EXPECT_EQ(t.tick(*r, 3), kF);
}

TEST(RetryTest, ARunningChildDoesNotUseUpAttempts) {
    ProbeNode* p = nullptr;
    const NodePtr r = retry(2, probe("a", nullptr, p));
    p->script({kR, kR, kF, kR, kF});
    Ticker t;
    EXPECT_EQ(t.tick(*r, 0), kR);
    EXPECT_EQ(t.tick(*r, 1), kR);
    EXPECT_EQ(t.tick(*r, 2), kR);  // attempt 1 failed
    EXPECT_EQ(t.tick(*r, 3), kR);
    EXPECT_EQ(t.tick(*r, 4), kF);  // attempt 2 failed
}

TEST(RetryTest, HaltResetsTheAttemptCount) {
    ProbeNode* p = nullptr;
    const NodePtr r = retry(2, probe("a", nullptr, p, kF));
    Ticker t;
    EXPECT_EQ(t.tick(*r, 0), kR);  // waiting for attempt 2
    r->halt();
    EXPECT_EQ(p->halts(), 0U);  // the child is not running between attempts
    EXPECT_EQ(r->state(), NodeState::Halted);
    EXPECT_EQ(t.tick(*r, 1), kR);  // attempt 1 again, not attempt 2
    EXPECT_EQ(t.tick(*r, 2), kF);
}

TEST(RetryTest, HaltHaltsARunningChild) {
    ProbeNode* p = nullptr;
    const NodePtr r = retry(2, probe("a", nullptr, p, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*r, 0), kR);
    r->halt();
    EXPECT_EQ(p->halts(), 1U);
}

// --- Timeout ------------------------------------------------------------------------

TEST(TimeoutTest, RejectsLimitsBelowOneTick) {
    ProbeNode* p = nullptr;
    EXPECT_THROW((void)timeout(Duration{0}, probe("a", nullptr, p)), std::invalid_argument);
    EXPECT_THROW((void)timeout(Duration{-3}, probe("a", nullptr, p)), std::invalid_argument);
}

TEST(TimeoutTest, PassesTheChildResultWithinTheLimit) {
    ProbeNode* p = nullptr;
    const NodePtr to = timeout(Duration{3}, probe("a", nullptr, p));
    p->script({kR, kR, kF});
    Ticker t;
    EXPECT_EQ(t.tick(*to, 10), kR);
    EXPECT_EQ(t.tick(*to, 11), kR);
    EXPECT_EQ(t.tick(*to, 12), kF);  // 12 - 10 < 3
}

TEST(TimeoutTest, PassesSuccessThroughAndGivesTheNextRunAFreshLimit) {
    ProbeNode* p = nullptr;
    const NodePtr to = timeout(Duration{3}, probe("a", nullptr, p));
    p->script({kS, kR, kR, kS, kR, kR, kR});
    Ticker t;
    EXPECT_EQ(t.tick(*to, 0), kS);  // immediate success
    EXPECT_EQ(t.tick(*to, 5), kR);  // run 2 starts at 5
    EXPECT_EQ(t.tick(*to, 6), kR);
    EXPECT_EQ(t.tick(*to, 7), kS);  // 7 - 5 < 3: success before the limit
    EXPECT_EQ(t.tick(*to, 8), kR);  // run 3 starts at 8, not 5
    EXPECT_EQ(t.tick(*to, 10), kR);
    EXPECT_EQ(t.tick(*to, 11), kF);  // 11 - 8 >= 3
    EXPECT_EQ(p->tickTimes(), ticksAt({0, 5, 6, 7, 8, 10}));
}

TEST(TimeoutTest, HaltsTheChildAndFailsAtTheLimit) {
    ProbeNode* p = nullptr;
    const NodePtr to = timeout(Duration{3}, probe("a", nullptr, p, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*to, 10), kR);
    EXPECT_EQ(t.tick(*to, 12), kR);
    EXPECT_EQ(t.tick(*to, 13), kF);  // 13 - 10 >= 3: not ticked again
    EXPECT_EQ(p->tickTimes(), ticksAt({10, 12}));
    EXPECT_EQ(p->halts(), 1U);
    EXPECT_EQ(p->state(), NodeState::Halted);
}

TEST(TimeoutTest, ALimitOfOneGivesTheChildOnlyItsFirstTick) {
    ProbeNode* p = nullptr;
    const NodePtr to = timeout(Duration{1}, probe("a", nullptr, p, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*to, 0), kR);
    EXPECT_EQ(t.tick(*to, 1), kF);
    EXPECT_EQ(p->ticks(), 1U);
}

TEST(TimeoutTest, EachRunGetsAFreshLimit) {
    ProbeNode* p = nullptr;
    const NodePtr to = timeout(Duration{2}, probe("a", nullptr, p, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*to, 0), kR);
    EXPECT_EQ(t.tick(*to, 2), kF);  // timed out
    EXPECT_EQ(t.tick(*to, 5), kR);  // new run starts at 5
    EXPECT_EQ(t.tick(*to, 6), kR);
    EXPECT_EQ(t.tick(*to, 7), kF);
}

TEST(TimeoutTest, AHaltedTimeoutRestartsItsClock) {
    ProbeNode* p = nullptr;
    const NodePtr to = timeout(Duration{2}, probe("a", nullptr, p, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*to, 0), kR);
    to->halt();
    EXPECT_EQ(p->halts(), 1U);
    EXPECT_EQ(t.tick(*to, 1), kR);  // started again at 1
    EXPECT_EQ(t.tick(*to, 2), kR);
    EXPECT_EQ(t.tick(*to, 3), kF);
}

// --- RateLimit ----------------------------------------------------------------------

TEST(RateLimitTest, RejectsPeriodsBelowOneTick) {
    ProbeNode* p = nullptr;
    EXPECT_THROW((void)rateLimit(Duration{0}, probe("a", nullptr, p)), std::invalid_argument);
}

TEST(RateLimitTest, StartsTheChildAtMostOncePerPeriod) {
    ProbeNode* p = nullptr;
    const NodePtr rl = rateLimit(Duration{3}, probe("a", nullptr, p));
    p->script({kS, kF});
    Ticker t;
    EXPECT_EQ(t.tick(*rl, 0), kS);
    EXPECT_EQ(t.tick(*rl, 1), kS);  // throttled: last result
    EXPECT_EQ(t.tick(*rl, 2), kS);
    EXPECT_EQ(t.tick(*rl, 3), kF);  // period over: runs again
    EXPECT_EQ(t.tick(*rl, 4), kF);
    EXPECT_EQ(p->tickTimes(), ticksAt({0, 3}));
}

TEST(RateLimitTest, TicksARunningChildEveryTick) {
    ProbeNode* p = nullptr;
    const NodePtr rl = rateLimit(Duration{10}, probe("a", nullptr, p));
    p->script({kR, kR, kS});
    Ticker t;
    EXPECT_EQ(t.tick(*rl, 0), kR);
    EXPECT_EQ(t.tick(*rl, 1), kR);
    EXPECT_EQ(t.tick(*rl, 2), kS);
    EXPECT_EQ(t.tick(*rl, 3), kS);  // throttled until 10
    EXPECT_EQ(t.tick(*rl, 9), kS);
    EXPECT_EQ(p->tickTimes(), ticksAt({0, 1, 2}));
    // The window counts from the start at 0, not from the completion at 2.
    EXPECT_EQ(t.tick(*rl, 10), kS);
    EXPECT_EQ(p->tickTimes(), ticksAt({0, 1, 2, 10}));
}

TEST(RateLimitTest, HaltDoesNotResetTheWindow) {
    ProbeNode* p = nullptr;
    const NodePtr rl = rateLimit(Duration{5}, probe("a", nullptr, p));
    p->script({kR});
    Ticker t;
    EXPECT_EQ(t.tick(*rl, 0), kR);
    rl->halt();
    EXPECT_EQ(p->halts(), 1U);
    // Started at 0 and never completed: throttled until 5, reporting Failure.
    EXPECT_EQ(t.tick(*rl, 1), kF);
    EXPECT_EQ(p->ticks(), 1U);
    EXPECT_EQ(t.tick(*rl, 5), kS);
    EXPECT_EQ(p->ticks(), 2U);
}

}  // namespace
}  // namespace mapc::bt
