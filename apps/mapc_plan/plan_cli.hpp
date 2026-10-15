#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace mapc::apps {

/// Exit codes of mapc_plan.
enum PlanCliExit : int {
    kPathFound = 0,
    kNoPath = 1,      ///< no path, or a search limit was reached
    kUsageError = 2,  ///< bad arguments or unreadable input
};

/// Runs the mapc_plan command line (without the program name) and returns
/// its exit code. Normal output goes to `out`, diagnostics to `err`.
///
///   mapc_plan grid  <map-file> <sx> <sy> <gx> <gy> [options]
///   mapc_plan graph <graph.yaml> <from> <to> [options]
int runPlanCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace mapc::apps
