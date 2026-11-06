#pragma once

#include <cstdint>

namespace grace::core {

/// Configuration of a FixedStepClock. All durations are integer nanoseconds.
struct FixedStepConfig
{
    std::int64_t step_ns;    ///< Duration of one fixed step; must be > 0.
    std::int64_t max_steps;  ///< Step cap per advance; must be > 0.
};

/// Deterministic fixed-step accumulator.
///
/// The clock keeps an integer-nanosecond accumulator: advance() adds the
/// elapsed nanoseconds and returns how many whole steps are due. The
/// remainder is preserved across calls, so the sequence of step counts is a
/// pure function of the sequence of advance() inputs — no floating-point
/// drift. When one advance would produce more than max_steps steps, only
/// max_steps are executed, the excess is discarded, and the clamp counter is
/// incremented.
class FixedStepClock
{
public:
    /// Throws std::invalid_argument when the configuration is invalid.
    explicit FixedStepClock(FixedStepConfig config);

    /// True when step_ns > 0 and max_steps > 0.
    static bool isValid(const FixedStepConfig config) noexcept;

    /// Adds elapsed_ns to the accumulator and returns the number of due
    /// steps (0..max_steps). Throws std::invalid_argument for negative input.
    std::int64_t advance(std::int64_t elapsed_ns);

    /// Clears the accumulator and all counters.
    void reset() noexcept;

    /// Nanoseconds carried into the next advance: 0 <= r < step_ns after a
    /// non-clamped advance, 0 after a clamp.
    std::int64_t remainderNs() const noexcept { return m_accumulator; }

    /// Total number of steps produced since construction or reset().
    std::uint64_t totalSteps() const noexcept { return m_total_steps; }

    /// Number of advances that exceeded the step cap and were clamped.
    std::uint64_t clampCount() const noexcept { return m_clamps; }

    const FixedStepConfig& config() const noexcept { return m_config; }

private:
    FixedStepConfig m_config;
    std::int64_t m_accumulator = 0;
    std::uint64_t m_total_steps = 0;
    std::uint64_t m_clamps = 0;
};

}  // namespace grace::core
