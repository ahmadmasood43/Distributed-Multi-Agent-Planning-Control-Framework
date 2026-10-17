#pragma once

#include "mapc/behavior/node.hpp"
#include "mapc/common/time.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mapc::bt {

/// A node with exactly one child that changes how the child's status is used.
class Decorator : public Node {
public:
    [[nodiscard]] std::size_t childCount() const noexcept override { return 1; }
    [[nodiscard]] const Node& child(std::size_t index) const override;

protected:
    /// Throws std::invalid_argument if `child` is null.
    Decorator(std::string name, NodePtr child);

    [[nodiscard]] Node& wrapped() noexcept { return *child_; }

    /// Halts the child (if it is running).
    void onHalt() noexcept override;

private:
    NodePtr child_;
};

/// Swaps Success and Failure; Running passes through.
class Inverter final : public Decorator {
public:
    explicit Inverter(NodePtr child, std::string name = {});
    [[nodiscard]] std::string typeName() const override { return "Inverter"; }

protected:
    Status onTick(TickContext& context) override;
};

/// Gives the child up to `max_attempts` attempts to succeed.
///
/// After a failed attempt that is not the last, Retry returns Running and
/// starts the next attempt on the next tick, not in the same one: within a
/// tick the world does not change, so an immediate retry of a synchronous
/// child would fail the same way. Fails after the last failed attempt.
/// Halting resets the attempt count.
class Retry final : public Decorator {
public:
    /// Throws std::invalid_argument if `max_attempts` is 0.
    Retry(std::uint32_t max_attempts, NodePtr child, std::string name = {});
    [[nodiscard]] std::string typeName() const override;

    [[nodiscard]] std::uint32_t maxAttempts() const noexcept { return max_attempts_; }
    /// Failed attempts in the current run.
    [[nodiscard]] std::uint32_t failedAttempts() const noexcept { return failed_; }

protected:
    Status onTick(TickContext& context) override;
    void onHalt() noexcept override;

private:
    std::uint32_t max_attempts_;
    std::uint32_t failed_ = 0;
};

/// Fails if the child is still running `limit` ticks of simulation time after
/// the Timeout started (its first tick after being idle, finished or halted).
///
/// The child is ticked only at times now with now - start < limit; on the
/// first tick at or past the limit the child is halted and Timeout fails
/// without ticking it. Timeout(1) therefore gives the child just the tick it
/// started in.
class Timeout final : public Decorator {
public:
    /// Throws std::invalid_argument unless `limit` >= 1 tick.
    Timeout(Duration limit, NodePtr child, std::string name = {});
    [[nodiscard]] std::string typeName() const override;

    [[nodiscard]] Duration limit() const noexcept { return limit_; }

protected:
    Status onTick(TickContext& context) override;

private:
    Duration limit_;
    Tick start_;
};

/// Starts the child at most once per `period` ticks of simulation time.
///
/// When the child is not running and the last start was less than `period`
/// ago, RateLimit does not tick it and returns the result of the child's last
/// completed run instead (Failure if no run has completed yet). A running
/// child is ticked every tick until it finishes; the limit applies to starts.
/// Use it for expensive checks or requests that need not run every tick.
///
/// The throttle window survives halt(): halting and restarting does not get
/// around the limit.
class RateLimit final : public Decorator {
public:
    /// Throws std::invalid_argument unless `period` >= 1 tick.
    RateLimit(Duration period, NodePtr child, std::string name = {});
    [[nodiscard]] std::string typeName() const override;

    [[nodiscard]] Duration period() const noexcept { return period_; }

protected:
    Status onTick(TickContext& context) override;

private:
    Duration period_;
    std::optional<Tick> last_start_;
    Status last_result_ = Status::Failure;
};

}  // namespace mapc::bt
