#include "mapc/planning/planner.hpp"

namespace mapc {

std::string_view toString(PlanFailure failure) noexcept {
    switch (failure) {
        case PlanFailure::InvalidRequest:
            return "invalid request";
        case PlanFailure::NoPath:
            return "no path";
        case PlanFailure::LimitReached:
            return "limit reached";
    }
    return "unknown failure";
}

}  // namespace mapc
