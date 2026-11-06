#include "grace/core/fixed_step_clock.hpp"

#include <limits>
#include <stdexcept>

namespace grace::core {

FixedStepClock::FixedStepClock(FixedStepConfig config) : m_config(config)
{
    if (!isValid(m_config))
        throw std::invalid_argument(
            "FixedStepClock: step_ns and max_steps must be positive");
}

bool FixedStepClock::isValid(const FixedStepConfig config) noexcept
{
    return config.step_ns > 0 && config.max_steps > 0;
}

std::int64_t FixedStepClock::advance(std::int64_t elapsed_ns)
{
    if (elapsed_ns < 0)
        throw std::invalid_argument("FixedStepClock: elapsed_ns must not be negative");

    const std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    if (elapsed_ns > kMax - m_accumulator)
    {
        // The accumulator would overflow: clamp to a full capped advance.
        m_accumulator = 0;
        ++m_clamps;
        m_total_steps += static_cast<std::uint64_t>(m_config.max_steps);
        return m_config.max_steps;
    }

    m_accumulator += elapsed_ns;
    std::int64_t steps = m_accumulator / m_config.step_ns;
    if (steps > m_config.max_steps)
    {
        steps = m_config.max_steps;
        m_accumulator = 0;
        ++m_clamps;
    }
    else
    {
        m_accumulator -= steps * m_config.step_ns;
    }
    m_total_steps += static_cast<std::uint64_t>(steps);
    return steps;
}

void FixedStepClock::reset() noexcept
{
    m_accumulator = 0;
    m_total_steps = 0;
    m_clamps = 0;
}

}  // namespace grace::core
