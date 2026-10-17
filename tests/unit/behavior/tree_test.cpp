#include "mapc/behavior/tree.hpp"

#include "mapc/behavior/builder.hpp"

#include "../support/bt_fixtures.hpp"
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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

// --- Node basics --------------------------------------------------------------------

TEST(NodeTest, StatusAndStateNames) {
    EXPECT_EQ(toString(Status::Success), "Success");
    EXPECT_EQ(toString(Status::Failure), "Failure");
    EXPECT_EQ(toString(Status::Running), "Running");
    EXPECT_EQ(toString(NodeState::Idle), "Idle");
    EXPECT_EQ(toString(NodeState::Running), "Running");
    EXPECT_EQ(toString(NodeState::Success), "Success");
    EXPECT_EQ(toString(NodeState::Failure), "Failure");
    EXPECT_EQ(toString(NodeState::Halted), "Halted");
}

TEST(NodeTest, StateFollowsTheLastTickAndHalt) {
    ProbeNode* p = nullptr;
    const NodePtr node = probe("a", nullptr, p);
    p->script({kR, kS, kF, kR});
    Ticker t;
    EXPECT_EQ(node->state(), NodeState::Idle);
    EXPECT_FALSE(node->running());
    (void)t.tick(*node, 0);
    EXPECT_TRUE(node->running());
    (void)t.tick(*node, 1);
    EXPECT_EQ(node->state(), NodeState::Success);
    (void)t.tick(*node, 2);
    EXPECT_EQ(node->state(), NodeState::Failure);
    (void)t.tick(*node, 3);
    node->halt();
    EXPECT_EQ(node->state(), NodeState::Halted);
    EXPECT_EQ(p->halts(), 1U);
}

TEST(NodeTest, HaltDoesNothingToANodeThatIsNotRunning) {
    ProbeNode* p = nullptr;
    const NodePtr node = probe("a", nullptr, p);
    node->halt();
    EXPECT_EQ(node->state(), NodeState::Idle);
    Ticker t;
    (void)t.tick(*node, 0);
    node->halt();
    EXPECT_EQ(node->state(), NodeState::Success);
    EXPECT_EQ(p->halts(), 0U);
}

TEST(NodeTest, LeavesHaveNoChildren) {
    ProbeNode* p = nullptr;
    const NodePtr node = probe("a", nullptr, p);
    EXPECT_EQ(node->childCount(), 0U);
    EXPECT_THROW((void)node->child(0), std::out_of_range);
}

class SelfTicking final : public Node {
public:
    SelfTicking() : Node("self") {}
    [[nodiscard]] std::string typeName() const override { return "SelfTicking"; }

protected:
    Status onTick(TickContext& context) override { return tick(context); }
};

TEST(NodeTest, ReentrantTicksAreRejected) {
    SelfTicking node;
    Ticker t;
    EXPECT_THROW((void)t.tick(node, 0), std::logic_error);
    EXPECT_EQ(node.state(), NodeState::Halted);
}

class BadStatus final : public Node {
public:
    BadStatus() : Node("bad") {}
    [[nodiscard]] std::string typeName() const override { return "BadStatus"; }

protected:
    Status onTick(TickContext& /*context*/) override { return static_cast<Status>(7); }
};

TEST(NodeTest, InvalidStatusesAreRejected) {
    EventLog log;
    ProbeNode* a = nullptr;
    const NodePtr seq = sequence("s", probe("a", &log, a, kR), node<BadStatus>());
    a->script({kS});
    Ticker t;
    EXPECT_THROW((void)t.tick(*seq, 0), std::logic_error);
    EXPECT_FALSE(seq->running());
}

// --- Exceptions leave nothing running -------------------------------------------------

TEST(NodeTest, AThrowingChildResetsItsAncestors) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    ProbeNode* c = nullptr;
    const NodePtr root =
        sequence("s", probe("a", &log, a), probe("b", &log, b), probe("c", &log, c));
    bool fail = true;
    b->onEachTick([&](TickContext&) {
        if (fail) {
            throw std::runtime_error("sensor offline");
        }
    });
    Ticker t;
    EXPECT_THROW((void)t.tick(*root, 0), std::runtime_error);
    EXPECT_EQ(root->state(), NodeState::Halted);
    EXPECT_EQ(b->state(), NodeState::Halted);
    // The sequence had moved past `a`; after the exception it starts over.
    fail = false;
    log.clear();
    EXPECT_EQ(t.tick(*root, 1), kS);
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "tick c"}));
}

TEST(NodeTest, AThrowingChildHaltsRunningSiblings) {
    EventLog log;
    ProbeNode* a = nullptr;
    ProbeNode* b = nullptr;
    const NodePtr root = parallel("p", 2, probe("a", &log, a, kR), probe("b", &log, b, kR));
    Ticker t;
    EXPECT_EQ(t.tick(*root, 0), kR);
    b->onEachTick([](TickContext&) { throw std::runtime_error("boom"); });
    log.clear();
    EXPECT_THROW((void)t.tick(*root, 1), std::runtime_error);
    // The throwing node is halted first, then its parent halts the rest.
    EXPECT_EQ(log, (EventLog{"tick a", "tick b", "halt b", "halt a"}));
    EXPECT_FALSE(a->running());
    EXPECT_FALSE(root->running());
}

// --- Leaves ------------------------------------------------------------------------

TEST(ConditionTest, MapsThePredicateToSuccessOrFailure) {
    bool value = true;
    const NodePtr cond = condition("ok?", [&](TickContext&) { return value; });
    Ticker t;
    EXPECT_EQ(t.tick(*cond, 0), kS);
    value = false;
    EXPECT_EQ(t.tick(*cond, 1), kF);
    EXPECT_EQ(cond->typeName(), "Condition");
    EXPECT_THROW(Condition("empty", {}), std::invalid_argument);
}

TEST(ActionTest, ReturnsItsFunctionsStatusAndCallsHaltOnlyWhileRunning) {
    Status next = kR;
    int halts = 0;
    const NodePtr act = action("work", [&](TickContext&) { return next; }, [&] { ++halts; });
    Ticker t;
    EXPECT_EQ(t.tick(*act, 0), kR);
    act->halt();
    EXPECT_EQ(halts, 1);
    next = kS;
    EXPECT_EQ(t.tick(*act, 1), kS);
    act->halt();
    EXPECT_EQ(halts, 1);
    EXPECT_EQ(act->typeName(), "Action");
    EXPECT_THROW(Action("empty", {}), std::invalid_argument);
}

TEST(ActionTest, SeesTimeAndBlackboard) {
    constexpr BlackboardKey<Tick::rep> kLast{"last"};
    const NodePtr act = action("stamp", [&](TickContext& context) {
        context.blackboard().set(kLast, context.now().value());
        return kS;
    });
    Ticker t;
    (void)t.tick(*act, 42);
    EXPECT_EQ(t.blackboard().get(kLast), 42);
}

class Countdown final : public StatefulAction {
public:
    Countdown(int steps, EventLog& log) : StatefulAction("countdown"), steps_(steps), log_(&log) {}

protected:
    Status onStart(TickContext& /*context*/) override {
        log_->emplace_back("start");
        left_ = steps_;
        return step();
    }
    Status onRunning(TickContext& /*context*/) override {
        log_->emplace_back("continue");
        return step();
    }
    void onHalted() noexcept override { log_->emplace_back("halted"); }

private:
    Status step() { return --left_ > 0 ? kR : kS; }

    int steps_;
    int left_ = 0;
    EventLog* log_;
};

TEST(StatefulActionTest, SeparatesStartContinuationAndHalt) {
    EventLog log;
    Countdown action(3, log);
    Ticker t;
    EXPECT_EQ(t.tick(action, 0), kR);
    EXPECT_EQ(t.tick(action, 1), kR);
    EXPECT_EQ(t.tick(action, 2), kS);
    EXPECT_EQ(t.tick(action, 3), kR);  // a new run
    action.halt();
    EXPECT_EQ(t.tick(action, 4), kR);  // halted, so it starts again
    EXPECT_EQ(log, (EventLog{"start", "continue", "continue", "start", "halted", "start"}));
    EXPECT_EQ(action.typeName(), "Action");
}

class ThrowingStart final : public StatefulAction {
public:
    explicit ThrowingStart(EventLog& log) : StatefulAction("throwing start"), log_(&log) {}

protected:
    Status onStart(TickContext& /*context*/) override {
        log_->emplace_back("start");
        throw std::runtime_error("could not start");
    }
    Status onRunning(TickContext& /*context*/) override { return kR; }
    void onHalted() noexcept override { log_->emplace_back("halted"); }

private:
    EventLog* log_;
};

TEST(StatefulActionTest, AThrowingStartIsCleanedUpByOnHalted) {
    EventLog log;
    ThrowingStart action(log);
    Ticker t;
    EXPECT_THROW((void)t.tick(action, 0), std::runtime_error);
    // Cleanup runs although the action never reached Running.
    EXPECT_EQ(log, (EventLog{"start", "halted"}));
    EXPECT_EQ(action.state(), NodeState::Halted);
}

TEST(ActionTest, AThrowingTickFunctionCallsTheHaltCallback) {
    int halts = 0;
    const NodePtr act = action(
        "throws", [](TickContext&) -> Status { throw std::runtime_error("x"); }, [&] { ++halts; });
    Ticker t;
    EXPECT_THROW((void)t.tick(*act, 0), std::runtime_error);
    EXPECT_EQ(halts, 1);
}

// --- Tree ---------------------------------------------------------------------------

TEST(TreeTest, RejectsANullRoot) {
    EXPECT_THROW(Tree(nullptr), std::invalid_argument);
}

TEST(TreeTest, TicksWithItsBlackboardAndRecordsTheTime) {
    constexpr BlackboardKey<int> kCount{"count"};
    Tree tree(action("count", [&](TickContext& context) {
        ++context.blackboard().get(kCount);
        return kR;
    }));
    tree.blackboard().set(kCount, 0);
    EXPECT_FALSE(tree.lastTick().has_value());
    EXPECT_EQ(tree.tick(Tick{5}), kR);
    EXPECT_EQ(tree.tick(Tick{5}), kR);  // the same time again is allowed
    EXPECT_EQ(tree.tick(Tick{9}), kR);
    EXPECT_EQ(tree.blackboard().get(kCount), 3);
    EXPECT_EQ(tree.lastTick(), Tick{9});
}

TEST(TreeTest, TimeMustNotGoBackwards) {
    Tree tree(action("a", [](TickContext&) { return kS; }));
    (void)tree.tick(Tick{10});
    EXPECT_THROW((void)tree.tick(Tick{9}), std::invalid_argument);
    EXPECT_EQ(tree.lastTick(), Tick{10});
}

TEST(TreeTest, HaltAndDestructionHaltRunningNodes) {
    int halts = 0;
    {
        Tree tree(sequence("s", action("a", [](TickContext&) { return kR; }, [&] { ++halts; })));
        (void)tree.tick(Tick{0});
        tree.halt();
        EXPECT_EQ(halts, 1);
        EXPECT_EQ(tree.root().state(), NodeState::Halted);
        (void)tree.tick(Tick{1});
    }
    EXPECT_EQ(halts, 2);  // the destructor halted the running action
}

TEST(TreeTest, CannotBeTickedOrHaltedFromInsideItsOwnTick) {
    Tree* self = nullptr;
    int mode = 0;
    Tree tree(action("meddle", [&](TickContext&) {
        if (mode == 1) {
            (void)self->tick(Tick{1});
        } else if (mode == 2) {
            self->halt();
        }
        return kS;
    }));
    self = &tree;
    mode = 1;
    EXPECT_THROW((void)tree.tick(Tick{0}), std::logic_error);
    mode = 2;
    EXPECT_THROW((void)tree.tick(Tick{1}), std::logic_error);
    mode = 0;
    EXPECT_EQ(tree.tick(Tick{2}), kS);
}

// --- Builder and dump ---------------------------------------------------------------

TEST(BuilderTest, BuildsTheShapeItDescribes) {
    std::vector<NodePtr> runtime_children;
    runtime_children.push_back(condition("x?", [](TickContext&) { return true; }));
    runtime_children.push_back(condition("y?", [](TickContext&) { return true; }));

    const NodePtr root = reactiveFallback(
        "root", sequence("from vector", std::move(runtime_children)),
        fallback("f", inverter(condition("c", [](TickContext&) { return false; }))),
        parallel("p", 1, retry(2, action("a", [](TickContext&) { return kS; })),
                 timeout(Duration{4},
                         rateLimit(Duration{2}, action("b", [](TickContext&) { return kS; })))),
        reactiveSequence("rs", node<ProbeNode>("probe", nullptr)));

    EXPECT_EQ(root->typeName(), "ReactiveFallback");
    ASSERT_EQ(root->childCount(), 4U);
    EXPECT_EQ(root->child(0).name(), "from vector");
    EXPECT_EQ(root->child(0).childCount(), 2U);
    EXPECT_EQ(root->child(1).child(0).typeName(), "Inverter");
    EXPECT_EQ(root->child(2).typeName(), "Parallel(1/2)");
    EXPECT_EQ(root->child(2).child(1).child(0).typeName(), "RateLimit(2)");
    EXPECT_EQ(root->child(3).child(0).typeName(), "Probe");
}

TEST(DumpTest, ShowsTypesNamesAndStatesAsAsciiTree) {
    bool fault = false;
    Tree tree(reactiveFallback(
        "agent",
        sequence("recover", condition("fault?", [&](TickContext&) { return fault; }),
                 action("recover", [](TickContext&) { return kR; })),
        sequence("work", retry(3, action("plan", [](TickContext&) { return kS; })),
                 action("follow", [](TickContext&) { return kR; })),
        action("idle", [](TickContext&) { return kS; })));

    EXPECT_EQ(tree.dump(),
              "ReactiveFallback \"agent\" [Idle]\n"
              "+-- Sequence \"recover\" [Idle]\n"
              "|   +-- Condition \"fault?\" [Idle]\n"
              "|   `-- Action \"recover\" [Idle]\n"
              "+-- Sequence \"work\" [Idle]\n"
              "|   +-- Retry(3) [Idle]\n"
              "|   |   `-- Action \"plan\" [Idle]\n"
              "|   `-- Action \"follow\" [Idle]\n"
              "`-- Action \"idle\" [Idle]\n");

    (void)tree.tick(Tick{0});
    fault = true;
    (void)tree.tick(Tick{1});
    EXPECT_EQ(tree.dump(),
              "ReactiveFallback \"agent\" [Running]\n"
              "+-- Sequence \"recover\" [Running]\n"
              "|   +-- Condition \"fault?\" [Success]\n"
              "|   `-- Action \"recover\" [Running]\n"
              "+-- Sequence \"work\" [Halted]\n"
              "|   +-- Retry(3) [Success]\n"
              "|   |   `-- Action \"plan\" [Success]\n"
              "|   `-- Action \"follow\" [Halted]\n"
              "`-- Action \"idle\" [Idle]\n");
    EXPECT_EQ(dumpTree(tree.root().child(2)), "Action \"idle\" [Idle]\n");
}

}  // namespace
}  // namespace mapc::bt
