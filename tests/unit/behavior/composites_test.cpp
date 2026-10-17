#include "mapc/behavior/composites.hpp"

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

// --- Construction -----------------------------------------------------------------

TEST(CompositeTest, RejectsEmptyAndNullChildren) {
    EXPECT_THROW(Sequence("s", {}), std::invalid_argument);
    EXPECT_THROW(Fallback("f", {}), std::invalid_argument);
    EXPECT_THROW(ReactiveSequence("rs", {}), std::invalid_argument);
    EXPECT_THROW(ReactiveFallback("rf", {}), std::invalid_argument);
    std::vector<NodePtr> with_null;
    with_null.push_back(action("a", [](TickContext&) { return kS; }));
    with_null.push_back(nullptr);
    EXPECT_THROW(Sequence("s", std::move(with_null)), std::invalid_argument);
}

TEST(CompositeTest, ExposesChildrenInOrder) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr seq = sequence("s", probe("a", &log, a), probe("b", &log, b));
    ASSERT_EQ(seq->childCount(), 2U);
    EXPECT_EQ(&seq->child(0), a);
    EXPECT_EQ(&seq->child(1), b);
    EXPECT_THROW((void)seq->child(2), std::out_of_range);
}

// --- Sequence -----------------------------------------------------------------------

TEST(SequenceTest, SucceedsWhenAllChildrenSucceed) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr seq = sequence("s", probe("a", &log, a), probe("b", &log, b));
    Ticker t;
    EXPECT_EQ(t.tick(*seq, 0), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
    EXPECT_EQ(seq->state(), NodeState::Success);
}

TEST(SequenceTest, StopsAtTheFirstFailure) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    const NodePtr seq =
        sequence("s", probe("a", &log, a), probe("b", &log, b, kF), probe("c", &log, c));
    Ticker t;
    EXPECT_EQ(t.tick(*seq, 0), kF);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
    EXPECT_EQ(c->state(), NodeState::Idle);
}

TEST(SequenceTest, ResumesAtTheRunningChildWithoutRecheckingEarlierOnes) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    const NodePtr seq =
        sequence("s", probe("a", &log, a), probe("b", &log, b), probe("c", &log, c));
    b->script({kR, kR});
    Ticker t;
    EXPECT_EQ(t.tick(*seq, 0), kR);
    EXPECT_EQ(seq->state(), NodeState::Running);
    EXPECT_EQ(t.tick(*seq, 1), kR);
    EXPECT_EQ(t.tick(*seq, 2), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "tick b", "tick b", "tick c"}));
}

TEST(SequenceTest, StartsAgainFromTheFirstChildAfterFinishing) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr seq = sequence("s", probe("a", &log, a), probe("b", &log, b));
    b->script({kF});
    Ticker t;
    EXPECT_EQ(t.tick(*seq, 0), kF);
    EXPECT_EQ(t.tick(*seq, 1), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "tick a", "tick b"}));
}

TEST(SequenceTest, HaltHaltsTheRunningChildAndRestartsFromTheFirst) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr seq = sequence("s", probe("a", &log, a), probe("b", &log, b, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*seq, 0), kR);
    seq->halt();
    EXPECT_EQ(seq->state(), NodeState::Halted);
    EXPECT_EQ(b->state(), NodeState::Halted);
    EXPECT_EQ(a->halts(), 0U);  // not running, so not halted
    log.clear();
    EXPECT_EQ(t.tick(*seq, 1), kR);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
}

// --- Fallback -----------------------------------------------------------------------

TEST(FallbackTest, SucceedsAtTheFirstSuccess) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    const NodePtr fb =
        fallback("f", probe("a", &log, a, kF), probe("b", &log, b), probe("c", &log, c));
    Ticker t;
    EXPECT_EQ(t.tick(*fb, 0), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
}

TEST(FallbackTest, FailsWhenAllChildrenFail) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr fb = fallback("f", probe("a", &log, a, kF), probe("b", &log, b, kF));
    Ticker t;
    EXPECT_EQ(t.tick(*fb, 0), kF);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
}

TEST(FallbackTest, ResumesAtTheRunningChild) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr fb = fallback("f", probe("a", &log, a, kF), probe("b", &log, b));
    b->script({kR});
    Ticker t;
    EXPECT_EQ(t.tick(*fb, 0), kR);
    EXPECT_EQ(t.tick(*fb, 1), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "tick b"}));
    // Finished, so the next tick starts at the first child again.
    log.clear();
    EXPECT_EQ(t.tick(*fb, 2), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
}

TEST(FallbackTest, HaltHaltsTheRunningChild) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr fb = fallback("f", probe("a", &log, a, kF), probe("b", &log, b, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*fb, 0), kR);
    fb->halt();
    EXPECT_EQ(b->halts(), 1U);
    log.clear();
    EXPECT_EQ(t.tick(*fb, 1), kR);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
}

// --- ReactiveSequence ---------------------------------------------------------------

TEST(ReactiveSequenceTest, RechecksEarlierChildrenEveryTick) {
    EventLog log;
    ProbeNode* cond = nullptr;
    ProbeNode* act = nullptr;
    const NodePtr rs =
        reactiveSequence("rs", probe("cond", &log, cond), probe("act", &log, act, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*rs, 0), kR);
    EXPECT_EQ(t.tick(*rs, 1), kR);
    EXPECT_EQ(log, (EventLog{"tick cond", "tick act", "tick cond", "tick act"}));
}

TEST(ReactiveSequenceTest, HaltsTheRunningChildWhenAnEarlierOneFails) {
    EventLog log;
    ProbeNode* cond = nullptr;
    ProbeNode* act = nullptr;
    const NodePtr rs =
        reactiveSequence("rs", probe("cond", &log, cond), probe("act", &log, act, kR));
    cond->script({kS, kF});
    Ticker t;
    EXPECT_EQ(t.tick(*rs, 0), kR);
    log.clear();
    EXPECT_EQ(t.tick(*rs, 1), kF);
    EXPECT_EQ(log, (EventLog{"tick cond", "halt act"}));
    EXPECT_EQ(act->state(), NodeState::Halted);
    EXPECT_EQ(rs->state(), NodeState::Failure);
}

TEST(ReactiveSequenceTest, HaltsALaterRunningChildWhenAnEarlierOneStartsRunning) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    const NodePtr rs =
        reactiveSequence("rs", probe("a", &log, a), probe("b", &log, b), probe("c", &log, c, kR));
    b->script({kS, kR});
    Ticker t;
    EXPECT_EQ(t.tick(*rs, 0), kR);  // c is running
    log.clear();
    EXPECT_EQ(t.tick(*rs, 1), kR);  // now b is running instead
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "halt c"}));
    EXPECT_EQ(b->state(), NodeState::Running);
    EXPECT_EQ(c->state(), NodeState::Halted);
}

TEST(ReactiveSequenceTest, SucceedsWhenAllChildrenSucceed) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr rs = reactiveSequence("rs", probe("a", &log, a), probe("b", &log, b));
    b->script({kR});
    Ticker t;
    EXPECT_EQ(t.tick(*rs, 0), kR);
    EXPECT_EQ(t.tick(*rs, 1), kS);
    EXPECT_EQ(b->halts(), 0U);
}

TEST(ReactiveSequenceTest, HaltHaltsTheRunningChild) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr rs = reactiveSequence("rs", probe("a", &log, a), probe("b", &log, b, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*rs, 0), kR);
    rs->halt();
    EXPECT_EQ(b->halts(), 1U);
    EXPECT_EQ(a->halts(), 0U);
    EXPECT_EQ(rs->state(), NodeState::Halted);
}

// --- ReactiveFallback ---------------------------------------------------------------

TEST(ReactiveFallbackTest, AHigherPriorityChildPreemptsARunningOne) {
    EventLog log;
    ProbeNode* urgent = nullptr;
    ProbeNode* normal = nullptr;
    const NodePtr rf = reactiveFallback("rf", probe("urgent", &log, urgent, kF),
                                        probe("normal", &log, normal, kR));
    urgent->script({kF, kR});
    Ticker t;
    EXPECT_EQ(t.tick(*rf, 0), kR);  // normal runs
    log.clear();
    EXPECT_EQ(t.tick(*rf, 1), kR);  // urgent takes over
    EXPECT_EQ(log, (EventLog{"tick urgent", "halt normal"}));
    EXPECT_EQ(normal->state(), NodeState::Halted);
    log.clear();
    EXPECT_EQ(t.tick(*rf, 2), kR);  // urgent done (fails again): normal restarts
    EXPECT_EQ(log, (EventLog{"tick urgent", "tick normal"}));
}

TEST(ReactiveFallbackTest, AnEarlierSuccessHaltsALaterRunningChild) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr rf = reactiveFallback("rf", probe("a", &log, a, kF), probe("b", &log, b, kR));
    a->script({kF, kS});
    Ticker t;
    EXPECT_EQ(t.tick(*rf, 0), kR);
    log.clear();
    EXPECT_EQ(t.tick(*rf, 1), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "halt b"}));
}

TEST(ReactiveFallbackTest, FailsWhenAllChildrenFail) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr rf = reactiveFallback("rf", probe("a", &log, a, kF), probe("b", &log, b, kF));
    Ticker t;
    EXPECT_EQ(t.tick(*rf, 0), kF);
    EXPECT_EQ(t.tick(*rf, 1), kF);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "tick a", "tick b"}));
}

TEST(ReactiveFallbackTest, HaltHaltsTheRunningChild) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr rf = reactiveFallback("rf", probe("a", &log, a, kF), probe("b", &log, b, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*rf, 0), kR);
    rf->halt();
    EXPECT_EQ(b->halts(), 1U);
    EXPECT_EQ(a->halts(), 0U);
}

// --- Parallel -----------------------------------------------------------------------

TEST(ParallelTest, RejectsThresholdsOutsideTheChildCount) {
    EventLog log;
    ProbeNode* p = nullptr;
    EXPECT_THROW((void)parallel("p", 0, probe("a", &log, p)), std::invalid_argument);
    EXPECT_THROW((void)parallel("p", 2, probe("a", &log, p)), std::invalid_argument);
    EXPECT_EQ(parallel("p", 1, probe("a", &log, p))->typeName(), "Parallel(1/1)");
}

TEST(ParallelTest, TicksAllChildrenEachTickWhileUndecided) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr par = parallel("p", 2, probe("a", &log, a, kR), probe("b", &log, b, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*par, 0), kR);
    EXPECT_EQ(t.tick(*par, 1), kR);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "tick a", "tick b"}));
}

TEST(ParallelTest, SucceedsAtTheThresholdAndHaltsTheRest) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    const NodePtr par =
        parallel("p", 2, probe("a", &log, a, kR), probe("b", &log, b, kR), probe("c", &log, c, kR));
    a->script({kS});
    c->script({kR, kS});
    Ticker t;
    EXPECT_EQ(t.tick(*par, 0), kR);  // a succeeded; b, c running
    log.clear();
    EXPECT_EQ(t.tick(*par, 1), kS);  // c succeeds: 2 of 3
    // a finished earlier and is not ticked again; b is halted.
    EXPECT_EQ(log, (EventLog{"tick b", "tick c", "halt b"}));
    EXPECT_EQ(b->state(), NodeState::Halted);
}

TEST(ParallelTest, FailsOnceTheThresholdIsOutOfReach) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    // 2 of 3 needed: the second failure decides, without ticking c.
    const NodePtr par =
        parallel("p", 2, probe("a", &log, a, kF), probe("b", &log, b, kF), probe("c", &log, c, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*par, 0), kF);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
    EXPECT_EQ(c->state(), NodeState::Idle);
}

TEST(ParallelTest, RemembersFailuresAcrossTicks) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    // 2 of 3 needed: one failure is tolerated, the second decides.
    const NodePtr par =
        parallel("p", 2, probe("a", &log, a, kS), probe("b", &log, b, kF), probe("c", &log, c, kR));
    a->script({kF});  // a fails first; ticked again it would succeed
    b->script({kR});
    Ticker t;
    EXPECT_EQ(t.tick(*par, 0), kR);
    log.clear();
    EXPECT_EQ(t.tick(*par, 1), kF);
    // a's failure is remembered (not re-ticked); b fails; c is halted.
    EXPECT_EQ(log, (EventLog{"tick b", "halt c"}));
    EXPECT_EQ(a->ticks(), 1U);
    EXPECT_EQ(c->state(), NodeState::Halted);
}

TEST(ParallelTest, DecidesAsSoonAsTheOutcomeIsCertain) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    const NodePtr par =
        parallel("p", 1, probe("a", &log, a, kR), probe("b", &log, b), probe("c", &log, c, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*par, 0), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "halt a"}));
}

TEST(ParallelTest, StartsAllChildrenAgainAfterFinishing) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr par = parallel("p", 2, probe("a", &log, a), probe("b", &log, b));
    b->script({kR});
    Ticker t;
    EXPECT_EQ(t.tick(*par, 0), kR);
    EXPECT_EQ(t.tick(*par, 1), kS);
    log.clear();
    EXPECT_EQ(t.tick(*par, 2), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
}

TEST(ParallelTest, HaltHaltsRunningChildrenAndForgetsResults) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr par = parallel("p", 2, probe("a", &log, a), probe("b", &log, b, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*par, 0), kR);
    par->halt();
    EXPECT_EQ(b->halts(), 1U);
    EXPECT_EQ(a->halts(), 0U);
    log.clear();
    // a's earlier success is forgotten: it runs again.
    EXPECT_EQ(t.tick(*par, 1), kR);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b"}));
}

}  // namespace
}  // namespace mapc::bt
