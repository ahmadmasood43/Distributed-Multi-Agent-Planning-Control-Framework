// Kuhn-Munkres (Hungarian) assignment with potentials, for rectangular
// matrices with n <= m rows and columns: each row in turn is added to the
// matching along a shortest augmenting path, keeping the reduced costs
// non-negative. O(n^2 * m).
//
// Costs are first shifted to [0, R] (subtracting the smallest feasible cost
// changes every full row matching by the same amount). Infeasible pairs then
// cost M = n * R + 1: a matching with fewer infeasible pairs is always
// cheaper, because the n - k feasible pairs of any matching sum to at most
// n * R < M. So the result has as many feasible pairs as possible and, among
// those, the smallest total cost.
//
// One extra "unassigned" column, also costing M, guarantees that every row
// can see an unmatched column whose potential is still zero. With
// non-negative costs that bounds every row potential and every real column
// potential: 0 <= u <= M and -M <= v <= 0, and every reduced cost is at most
// 2M. With R <= 2^41 and n <= 2^20, 2M < 2^63, so 64-bit arithmetic is exact.
// The virtual column 0 has no such bound (it would collect every delta of
// every phase), so its potential is not maintained; nothing reads it.

#include "mapc/allocation/task_allocator.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace mapc {

std::vector<MatrixMatch> HungarianAllocator::solve(const CostMatrix& costs) const {
    // Rows are the smaller side, so the matrix may be transposed.
    const bool transposed = costs.agentCount() > costs.taskCount();
    const std::size_t n = transposed ? costs.taskCount() : costs.agentCount();
    const std::size_t m = transposed ? costs.agentCount() : costs.taskCount();
    if (n == 0) {
        return {};
    }
    const auto cost = [&](std::size_t row, std::size_t col) {
        return transposed ? costs.at(col, row) : costs.at(row, col);
    };

    TaskCost::rep lowest = TaskCost::kLimit;
    TaskCost::rep highest = -TaskCost::kLimit;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < m; ++j) {
            if (const TaskCost c = cost(i, j); c.feasible()) {
                lowest = std::min(lowest, c.value());
                highest = std::max(highest, c.value());
            }
        }
    }
    const std::int64_t range = lowest <= highest ? highest - lowest : 0;
    const std::int64_t unassigned = (static_cast<std::int64_t>(n) * range) + 1;

    // 1-based, as in the textbook formulation: column 0 is a virtual column
    // that holds the row being inserted, and column m + 1 means "unassigned".
    const std::size_t cols = m + 2;
    std::vector<std::int64_t> a((n + 1) * cols, unassigned);
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            if (const TaskCost c = cost(i - 1, j - 1); c.feasible()) {
                a[(i * cols) + j] = c.value() - lowest;
            }
        }
    }

    constexpr std::int64_t kInfinity = std::numeric_limits<std::int64_t>::max();
    const std::size_t last = m + 1;
    std::vector<std::int64_t> u(n + 1, 0);      // row potentials
    std::vector<std::int64_t> v(last + 1, 0);   // column potentials
    std::vector<std::size_t> p(last + 1, 0);    // p[j]: row matched to column j (0 = none)
    std::vector<std::size_t> way(last + 1, 0);  // previous column on the augmenting path
    std::vector<std::int64_t> minv(last + 1);
    std::vector<bool> used(last + 1);

    for (std::size_t i = 1; i <= n; ++i) {
        p[0] = i;
        std::size_t j0 = 0;
        std::fill(minv.begin(), minv.end(), kInfinity);
        std::fill(used.begin(), used.end(), false);
        // Grow a shortest-path tree until it reaches a free column.
        for (;;) {
            used[j0] = true;
            const std::size_t i0 = p[j0];
            std::int64_t delta = kInfinity;
            std::size_t j1 = 0;
            for (std::size_t j = 1; j <= last; ++j) {
                if (used[j]) {
                    continue;
                }
                const std::int64_t reduced = a[(i0 * cols) + j] - u[i0] - v[j];
                if (reduced < minv[j]) {
                    minv[j] = reduced;
                    way[j] = j0;
                }
                // Any column with the smallest reduced cost may come next.
                // Preferring a free one ends the search early, which keeps
                // matrices full of equal (for example infeasible) costs at
                // O(n * m) instead of growing the whole tree every phase.
                if (minv[j] < delta || (minv[j] == delta && p[j] == 0 && p[j1] != 0)) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (std::size_t j = 0; j <= last; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    // Column 0 only carries the row being inserted. Its
                    // potential is never read, and would accumulate every
                    // delta of every phase, so it is not kept.
                    if (j != 0) {
                        v[j] -= delta;
                    }
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
            if (p[j0] == 0 || j0 == last) {
                break;
            }
        }
        // Flip the augmenting path (j0 is a real or the "unassigned" column).
        while (j0 != 0) {
            const std::size_t j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        }
        // The "unassigned" column is never kept: it stays free (with potential
        // zero) for the next row, which is what bounds the potentials.
        p[last] = 0;
    }

    std::vector<MatrixMatch> matches;
    for (std::size_t j = 1; j <= m; ++j) {
        if (p[j] == 0) {
            continue;
        }
        const MatrixMatch match =
            transposed ? MatrixMatch{j - 1, p[j] - 1} : MatrixMatch{p[j] - 1, j - 1};
        if (costs.at(match.agent, match.task).feasible()) {
            matches.push_back(match);
        }
    }
    std::sort(matches.begin(), matches.end(),
              [](MatrixMatch x, MatrixMatch y) { return x.agent < y.agent; });
    return matches;
}

}  // namespace mapc
