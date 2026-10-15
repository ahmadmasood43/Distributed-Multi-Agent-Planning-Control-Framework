#include "plan_cli.hpp"

#include "mapc/graph/grid_map.hpp"
#include "mapc/io/graph_yaml.hpp"
#include "mapc/planning/best_first_search.hpp"
#include "mapc/planning/heuristics.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace mapc::apps {

namespace {

constexpr const char* kUsage =
    "usage: mapc_plan grid  <map-file> <sx> <sy> <gx> <gy> [options]\n"
    "       mapc_plan graph <graph.yaml> <from> <to> [options]\n"
    "\n"
    "Plans a single-agent path on a MovingAI grid map or a YAML graph.\n"
    "\n"
    "options:\n"
    "  --planner <dijkstra|astar|wastar>  search algorithm (default: astar)\n"
    "  --weight <w>                       heuristic weight for wastar, 1..1000 (default: 1.5)\n"
    "  --eight                            8-connected grid moves (straight 10, diagonal 14)\n"
    "  --max-expansions <n>               stop after n expansions (default: unlimited)\n"
    "  --start-time <t>                   start tick (default: 0)\n"
    "  --help                             show this help\n"
    "\n"
    "exit status: 0 path found, 1 no path or limit reached, 2 usage or input error\n";

struct Options {
    std::string planner = "astar";
    double weight = 1.5;
    bool eight = false;
    std::uint64_t max_expansions = 0;
    std::int64_t start_time = 0;
    std::vector<std::string> positional;
};

/// Strict decimal integer parsing: optional '-', digits only, no trailing
/// text, and within [min, max]. std::from_chars reports out-of-range values
/// instead of overflowing.
std::optional<std::int64_t> parseInt(std::string_view text, std::int64_t min, std::int64_t max) {
    std::int64_t value = 0;
    const char* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (text.empty() || ec != std::errc() || ptr != end || value < min || value > max) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> parseDouble(const std::string& text) {
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

/// Parses options; returns an error message, or empty on success.
std::string parseOptions(const std::vector<std::string>& args, Options& options, bool& help) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&](const char* name) -> std::optional<std::string> {
            if (i + 1 >= args.size()) {
                return std::nullopt;
            }
            (void)name;
            return args[++i];
        };
        if (arg == "--help" || arg == "-h") {
            help = true;
        } else if (arg == "--eight") {
            options.eight = true;
        } else if (arg == "--planner") {
            const auto v = value("--planner");
            if (!v) {
                return "--planner needs a value";
            }
            options.planner = *v;
        } else if (arg == "--weight") {
            const auto v = value("--weight");
            const auto w = v ? parseDouble(*v) : std::nullopt;
            if (!w) {
                return "--weight needs a number";
            }
            options.weight = *w;
        } else if (arg == "--max-expansions") {
            const auto v = value("--max-expansions");
            const auto n =
                v ? parseInt(*v, 1, std::numeric_limits<std::int64_t>::max()) : std::nullopt;
            if (!n) {
                return "--max-expansions needs a positive integer";
            }
            options.max_expansions = static_cast<std::uint64_t>(*n);
        } else if (arg == "--start-time") {
            const auto v = value("--start-time");
            const auto t =
                v ? parseInt(*v, 0, std::numeric_limits<std::int64_t>::max()) : std::nullopt;
            if (!t) {
                return "--start-time needs a non-negative integer";
            }
            options.start_time = *t;
        } else if (arg.size() > 1 && arg.front() == '-' &&
                   !parseInt(arg, std::numeric_limits<std::int64_t>::min(),
                             std::numeric_limits<std::int64_t>::max())) {
            return "unknown option '" + arg + "'";
        } else {
            options.positional.push_back(arg);
        }
    }
    return "";
}

std::unique_ptr<Planner> makePlanner(const Options& options,
                                     std::shared_ptr<const Heuristic> heuristic,
                                     std::string& error) {
    try {
        if (options.planner == "dijkstra") {
            return std::make_unique<DijkstraPlanner>();
        }
        if (options.planner == "astar") {
            return std::make_unique<AStarPlanner>(std::move(heuristic));
        }
        if (options.planner == "wastar") {
            return std::make_unique<WeightedAStarPlanner>(
                std::move(heuristic), HeuristicWeight::fromDouble(options.weight));
        }
        error = "unknown planner '" + options.planner + "' (expected dijkstra, astar or wastar)";
    } catch (const std::invalid_argument& e) {
        error = e.what();
    }
    return nullptr;
}

/// Prints the result; returns the exit code.
int report(const Planner& planner, const PlanResult& result, std::ostream& out,
           const std::function<std::string(NodeId)>& label) {
    out << "planner:   " << planner.name() << '\n';
    out << "status:    "
        << (result.found() ? "found" : std::string(toString(result.outcome.error()))) << '\n';
    out << "expanded:  " << result.stats.expanded << '\n';
    out << "generated: " << result.stats.generated << '\n';
    if (!result.found()) {
        return kNoPath;
    }
    const Path& path = *result.outcome;
    out << "cost:      " << path.cost.count() << '\n';
    out << "steps:     " << path.steps.size() << '\n';
    out << "path:     ";
    for (const PathStep& step : path.steps) {
        out << ' ' << label(step.node) << '@' << step.time.value();
    }
    out << '\n';
    return kPathFound;
}

int runGrid(const Options& options, std::ostream& out, std::ostream& err) {
    if (options.positional.size() != 6) {
        err << "grid mode needs <map-file> <sx> <sy> <gx> <gy>\n" << kUsage;
        return kUsageError;
    }
    GridOptions grid_options;
    if (options.eight) {
        grid_options.connectivity = GridConnectivity::Eight;
        grid_options.cardinal_cost = Duration{10};
        grid_options.diagonal_cost = Duration{14};
    }
    auto loaded = GridMap::loadMovingAi(options.positional[1], grid_options);
    if (!loaded) {
        err << "error: " << loaded.error().describe() << '\n';
        return kUsageError;
    }
    const GridMap& map = *loaded;

    std::array<NodeId, 2> cells{};
    for (std::size_t k = 0; k < 2; ++k) {
        const std::size_t base = 2 + 2 * k;
        const auto x = parseInt(options.positional[base], 0, UINT32_MAX);
        const auto y = parseInt(options.positional[base + 1], 0, UINT32_MAX);
        const char* what = k == 0 ? "start" : "goal";
        if (!x || !y) {
            err << "error: " << what << " coordinates must be non-negative integers\n";
            return kUsageError;
        }
        const auto node =
            map.nodeAt(static_cast<std::uint32_t>(*x), static_cast<std::uint32_t>(*y));
        if (!node) {
            err << "error: " << what << " (" << *x << ", " << *y
                << ") is not a passable cell of the map\n";
            return kUsageError;
        }
        cells[k] = *node;
    }

    std::string error;
    const auto planner = makePlanner(options, std::make_shared<GridHeuristic>(map), error);
    if (!planner) {
        err << "error: " << error << '\n';
        return kUsageError;
    }
    PlanRequest request;
    request.start = cells[0];
    request.goal = cells[1];
    request.start_time = Tick{options.start_time};
    request.limits.max_expansions = options.max_expansions;
    const PlanResult result = planner->plan(GraphView(map.graph()), request);
    return report(*planner, result, out, [&](NodeId node) {
        const GridCell cell = map.cellOf(node);
        return "(" + std::to_string(cell.x) + "," + std::to_string(cell.y) + ")";
    });
}

int runGraph(const Options& options, std::ostream& out, std::ostream& err) {
    if (options.positional.size() != 4) {
        err << "graph mode needs <graph.yaml> <from> <to>\n" << kUsage;
        return kUsageError;
    }
    auto loaded = io::loadGraphYaml(options.positional[1]);
    if (!loaded) {
        err << "error: " << loaded.error().describe() << '\n';
        return kUsageError;
    }
    const io::NamedGraph& named = *loaded;
    const auto start = named.find(options.positional[2]);
    const auto goal = named.find(options.positional[3]);
    if (!start || !goal) {
        err << "error: unknown node '" << (start ? options.positional[3] : options.positional[2])
            << "'\n";
        return kUsageError;
    }

    std::shared_ptr<const Heuristic> heuristic;
    if (auto euclidean = EuclideanHeuristic::create(named.graph)) {
        heuristic = std::make_shared<EuclideanHeuristic>(std::move(euclidean).value());
    } else {
        heuristic = std::make_shared<ZeroHeuristic>();
        if (options.planner != "dijkstra") {
            out << "note:      nodes have no positions; using the zero heuristic\n";
        }
    }
    std::string error;
    const auto planner = makePlanner(options, heuristic, error);
    if (!planner) {
        err << "error: " << error << '\n';
        return kUsageError;
    }
    PlanRequest request;
    request.start = *start;
    request.goal = *goal;
    request.start_time = Tick{options.start_time};
    request.limits.max_expansions = options.max_expansions;
    const PlanResult result = planner->plan(GraphView(named.graph), request);
    return report(*planner, result, out, [&](NodeId node) { return named.nameOf(node); });
}

}  // namespace

int runPlanCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    Options options;
    bool help = false;
    if (const std::string error = parseOptions(args, options, help); !error.empty()) {
        err << "error: " << error << '\n' << kUsage;
        return kUsageError;
    }
    if (help) {
        out << kUsage;
        return kPathFound;
    }
    if (options.positional.empty()) {
        err << kUsage;
        return kUsageError;
    }
    const std::string& mode = options.positional.front();
    if (mode == "grid") {
        return runGrid(options, out, err);
    }
    if (mode == "graph") {
        return runGraph(options, out, err);
    }
    err << "error: unknown mode '" << mode << "' (expected grid or graph)\n" << kUsage;
    return kUsageError;
}

}  // namespace mapc::apps
