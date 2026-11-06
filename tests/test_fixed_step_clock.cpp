#include "grace/core/fixed_step_clock.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "test_support.hpp"

namespace {

using grace::core::FixedStepClock;
using grace::core::FixedStepConfig;

bool throwsInvalidArgument(FixedStepConfig config)
{
    try
    {
        const FixedStepClock clock(config);
        static_cast<void>(clock);
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    // Invalid configuration rejection.
    GRACE_CHECK(!FixedStepClock::isValid(FixedStepConfig{0, 5}));
    GRACE_CHECK(!FixedStepClock::isValid(FixedStepConfig{-1, 5}));
    GRACE_CHECK(!FixedStepClock::isValid(FixedStepConfig{10, 0}));
    GRACE_CHECK(!FixedStepClock::isValid(FixedStepConfig{10, -3}));
    GRACE_CHECK(FixedStepClock::isValid(FixedStepConfig{10, 5}));
    GRACE_CHECK(throwsInvalidArgument(FixedStepConfig{0, 5}));
    GRACE_CHECK(throwsInvalidArgument(FixedStepConfig{10, 0}));

    // Remainder preservation across advances.
    {
        FixedStepClock clock(FixedStepConfig{10, 100});
        GRACE_CHECK_EQ(clock.advance(4), 0);
        GRACE_CHECK_EQ(clock.remainderNs(), 4);
        GRACE_CHECK_EQ(clock.advance(4), 0);
        GRACE_CHECK_EQ(clock.remainderNs(), 8);
        GRACE_CHECK_EQ(clock.advance(4), 1);
        GRACE_CHECK_EQ(clock.remainderNs(), 2);
        GRACE_CHECK_EQ(clock.totalSteps(), 1u);
        GRACE_CHECK_EQ(clock.clampCount(), 0u);
    }

    // Exact multiples leave no remainder.
    {
        FixedStepClock clock(FixedStepConfig{10, 100});
        GRACE_CHECK_EQ(clock.advance(30), 3);
        GRACE_CHECK_EQ(clock.remainderNs(), 0);
        GRACE_CHECK_EQ(clock.totalSteps(), 3u);
        GRACE_CHECK_EQ(clock.clampCount(), 0u);
    }

    // Determinism: identical input sequences give identical outputs.
    {
        FixedStepClock a(FixedStepConfig{7, 4});
        FixedStepClock b(FixedStepConfig{7, 4});
        const std::int64_t inputs[] = {1, 100, 3, 42, 7, 1000, 5, 0, 13};
        bool same = true;
        for (const std::int64_t input : inputs)
        {
            const std::int64_t steps_a = a.advance(input);
            const std::int64_t steps_b = b.advance(input);
            if (steps_a != steps_b || a.remainderNs() != b.remainderNs())
                same = false;
        }
        GRACE_CHECK(same);
        GRACE_CHECK_EQ(a.totalSteps(), b.totalSteps());
        GRACE_CHECK_EQ(a.clampCount(), b.clampCount());
    }

    // Clamp behaviour: excess beyond the cap is discarded and counted.
    {
        FixedStepClock clock(FixedStepConfig{10, 5});
        GRACE_CHECK_EQ(clock.advance(1000), 5);
        GRACE_CHECK_EQ(clock.remainderNs(), 0);
        GRACE_CHECK_EQ(clock.clampCount(), 1u);
        GRACE_CHECK_EQ(clock.totalSteps(), 5u);
        GRACE_CHECK_EQ(clock.advance(1000), 5);
        GRACE_CHECK_EQ(clock.clampCount(), 2u);
        // Just below the cap is not clamped and keeps its remainder.
        GRACE_CHECK_EQ(clock.advance(49), 4);
        GRACE_CHECK_EQ(clock.remainderNs(), 9);
        GRACE_CHECK_EQ(clock.clampCount(), 2u);
        GRACE_CHECK_EQ(clock.advance(1), 1);
        GRACE_CHECK_EQ(clock.remainderNs(), 0);
    }

    // Negative elapsed time is rejected.
    {
        FixedStepClock clock(FixedStepConfig{10, 5});
        bool threw = false;
        try
        {
            static_cast<void>(clock.advance(-1));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
    }

    // Accumulator overflow is clamped instead of wrapping.
    {
        FixedStepClock clock(FixedStepConfig{10, 5});
        const std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
        GRACE_CHECK_EQ(clock.advance(kMax), 5);
        GRACE_CHECK_EQ(clock.advance(kMax), 5);
        GRACE_CHECK_EQ(clock.clampCount(), 2u);
        GRACE_CHECK_EQ(clock.remainderNs(), 0);
    }

    // reset() clears the accumulator and all counters.
    {
        FixedStepClock clock(FixedStepConfig{10, 5});
        static_cast<void>(clock.advance(25));
        clock.reset();
        GRACE_CHECK_EQ(clock.remainderNs(), 0);
        GRACE_CHECK_EQ(clock.totalSteps(), 0u);
        GRACE_CHECK_EQ(clock.clampCount(), 0u);
        GRACE_CHECK_EQ(clock.advance(10), 1);
    }

    return grace::test::summary("fixed_step_clock");
}
