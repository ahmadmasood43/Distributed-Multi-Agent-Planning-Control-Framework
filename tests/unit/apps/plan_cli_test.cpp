#include "plan_cli.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#ifndef MAPC_TEST_DATA_DIR
#error "MAPC_TEST_DATA_DIR must point at tests/data"
#endif

namespace mapc::apps {
namespace {

const std::string kData = MAPC_TEST_DATA_DIR;

struct CliRun {
    int code;
    std::string out;
    std::string err;
};

CliRun run(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = runPlanCli(args, out, err);
    return CliRun{code, out.str(), err.str()};
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// --- Grid mode ---------------------------------------------------------------------

TEST(PlanCliTest, GridFindsShortestPath) {
    const CliRun r = run({"grid", kData + "/small.map", "0", "0", "4", "2"});
    EXPECT_EQ(r.code, kPathFound) << r.err;
    EXPECT_TRUE(contains(r.out, "planner:   astar"));
    EXPECT_TRUE(contains(r.out, "status:    found"));
    EXPECT_TRUE(contains(r.out, "cost:      8"));
    EXPECT_TRUE(contains(r.out, "(0,0)@0"));
    EXPECT_TRUE(contains(r.out, "(4,2)@8"));
    EXPECT_TRUE(r.err.empty());
}

TEST(PlanCliTest, AllPlannersAgreeOnOptimalCost) {
    for (const char* planner : {"dijkstra", "astar"}) {
        const CliRun r =
            run({"grid", kData + "/small.map", "0", "0", "4", "2", "--planner", planner});
        EXPECT_EQ(r.code, kPathFound) << planner;
        EXPECT_TRUE(contains(r.out, "cost:      8")) << planner;
    }
    const CliRun w = run(
        {"grid", kData + "/small.map", "0", "0", "4", "2", "--planner", "wastar", "--weight", "2"});
    EXPECT_EQ(w.code, kPathFound);
    EXPECT_TRUE(contains(w.out, "planner:   wastar"));
}

TEST(PlanCliTest, EightConnectedGridUsesDiagonalCosts) {
    const CliRun r = run({"grid", kData + "/small.map", "0", "0", "5", "3", "--eight"});
    EXPECT_EQ(r.code, kPathFound) << r.err;
    EXPECT_TRUE(contains(r.out, "status:    found"));
    // Straight moves cost 10 and diagonals 14, so costs are no longer step counts.
    EXPECT_FALSE(contains(r.out, "cost:      8\n"));
}

TEST(PlanCliTest, LargestValidValuesAreAccepted) {
    const CliRun r = run({"grid", kData + "/small.map", "0", "0", "4", "2", "--start-time",
                          "9223372036854775807", "--max-expansions", "9223372036854775807"});
    EXPECT_EQ(r.code, kPathFound) << r.err;
    // Path times saturate at the end of the timeline instead of overflowing.
    EXPECT_TRUE(contains(r.out, "(4,2)@9223372036854775807"));
}

TEST(PlanCliTest, StartTimeAndExpansionLimit) {
    const CliRun timed =
        run({"grid", kData + "/small.map", "0", "0", "4", "2", "--start-time", "100"});
    EXPECT_EQ(timed.code, kPathFound);
    EXPECT_TRUE(contains(timed.out, "(0,0)@100"));
    EXPECT_TRUE(contains(timed.out, "(4,2)@108"));

    const CliRun limited =
        run({"grid", kData + "/small.map", "0", "0", "4", "2", "--max-expansions", "1"});
    EXPECT_EQ(limited.code, kNoPath);
    EXPECT_TRUE(contains(limited.out, "status:    limit reached"));
}

TEST(PlanCliTest, GridInputErrors) {
    EXPECT_EQ(run({"grid", kData + "/small.map", "0", "0", "4"}).code, kUsageError);
    const CliRun blocked = run({"grid", kData + "/small.map", "0", "0", "1", "1"});
    EXPECT_EQ(blocked.code, kUsageError);
    EXPECT_TRUE(contains(blocked.err, "goal (1, 1) is not a passable cell"));
    const CliRun outside = run({"grid", kData + "/small.map", "99", "0", "1", "0"});
    EXPECT_EQ(outside.code, kUsageError);
    EXPECT_TRUE(contains(outside.err, "start (99, 0)"));
    const CliRun negative = run({"grid", kData + "/small.map", "-1", "0", "1", "0"});
    EXPECT_EQ(negative.code, kUsageError);
    EXPECT_TRUE(contains(negative.err, "non-negative integers"));
    const CliRun text = run({"grid", kData + "/small.map", "a", "0", "1", "0"});
    EXPECT_EQ(text.code, kUsageError);
    for (const char* huge : {"4294967296", "18446744073709551616", "9223372036854775808"}) {
        const CliRun r = run({"grid", kData + "/small.map", huge, "0", "1", "0"});
        EXPECT_EQ(r.code, kUsageError) << huge;
        EXPECT_TRUE(contains(r.err, "non-negative integers")) << huge;
    }
    const CliRun missing = run({"grid", kData + "/missing.map", "0", "0", "1", "0"});
    EXPECT_EQ(missing.code, kUsageError);
    EXPECT_TRUE(contains(missing.err, "cannot open"));
}

// --- Graph mode -------------------------------------------------------------------

TEST(PlanCliTest, GraphUsesNodeNames) {
    const CliRun r = run({"graph", kData + "/roads.yaml", "depot", "shop"});
    EXPECT_EQ(r.code, kPathFound) << r.err;
    EXPECT_TRUE(contains(r.out, "cost:      12"));
    EXPECT_TRUE(contains(r.out, "depot@0 cross@5 shop@12"));
}

TEST(PlanCliTest, GraphReportsUnreachableGoal) {
    const CliRun r = run({"graph", kData + "/roads.yaml", "depot", "island"});
    EXPECT_EQ(r.code, kNoPath);
    EXPECT_TRUE(contains(r.out, "status:    no path"));
}

TEST(PlanCliTest, GraphWithoutPositionsFallsBackToZeroHeuristic) {
    const CliRun r = run({"graph", kData + "/unpositioned.yaml", "a", "c"});
    EXPECT_EQ(r.code, kPathFound) << r.err;
    EXPECT_TRUE(contains(r.out, "zero heuristic"));
    const CliRun quiet =
        run({"graph", kData + "/unpositioned.yaml", "a", "c", "--planner", "dijkstra"});
    EXPECT_FALSE(contains(quiet.out, "zero heuristic"));
}

TEST(PlanCliTest, GraphInputErrors) {
    EXPECT_EQ(run({"graph", kData + "/roads.yaml", "depot"}).code, kUsageError);
    const CliRun unknown_from = run({"graph", kData + "/roads.yaml", "nowhere", "shop"});
    EXPECT_EQ(unknown_from.code, kUsageError);
    EXPECT_TRUE(contains(unknown_from.err, "unknown node 'nowhere'"));
    const CliRun unknown_to = run({"graph", kData + "/roads.yaml", "depot", "nowhere"});
    EXPECT_TRUE(contains(unknown_to.err, "unknown node 'nowhere'"));
    const CliRun bad_file = run({"graph", kData + "/small.map", "a", "b"});
    EXPECT_EQ(bad_file.code, kUsageError);
}

// --- Options and usage ----------------------------------------------------------------

TEST(PlanCliTest, HelpAndUsage) {
    const CliRun help = run({"--help"});
    EXPECT_EQ(help.code, kPathFound);
    EXPECT_TRUE(contains(help.out, "usage: mapc_plan"));
    EXPECT_EQ(run({}).code, kUsageError);
    const CliRun mode = run({"maze"});
    EXPECT_EQ(mode.code, kUsageError);
    EXPECT_TRUE(contains(mode.err, "unknown mode 'maze'"));
}

TEST(PlanCliTest, OptionErrors) {
    const std::string map = kData + "/small.map";
    const std::vector<std::vector<std::string>> bad{
        {"grid", map, "0", "0", "4", "2", "--planner"},
        {"grid", map, "0", "0", "4", "2", "--planner", "bfs"},
        {"grid", map, "0", "0", "4", "2", "--planner", "wastar", "--weight", "0.5"},
        {"grid", map, "0", "0", "4", "2", "--weight", "abc"},
        {"grid", map, "0", "0", "4", "2", "--weight"},
        {"grid", map, "0", "0", "4", "2", "--max-expansions", "0"},
        {"grid", map, "0", "0", "4", "2", "--max-expansions", "-3"},
        {"grid", map, "0", "0", "4", "2", "--start-time", "-1"},
        {"grid", map, "0", "0", "4", "2", "--start-time"},
        {"grid", map, "0", "0", "4", "2", "--frobnicate"},
        // Out-of-range values must be rejected, not overflow while parsing.
        {"grid", map, "0", "0", "4", "2", "--start-time", "9223372036854775808"},
        {"grid", map, "0", "0", "4", "2", "--start-time", "99999999999999999999999"},
        {"grid", map, "0", "0", "4", "2", "--max-expansions", "9223372036854775808"},
        {"grid", map, "0", "0", "4", "2", "--start-time", "+5"},
        {"grid", map, "0", "0", "4", "2", "--start-time", " 5"},
    };
    for (const auto& args : bad) {
        const CliRun r = run(args);
        EXPECT_EQ(r.code, kUsageError) << args.back();
        EXPECT_TRUE(contains(r.err, "error:")) << args.back();
    }
}

}  // namespace
}  // namespace mapc::apps
