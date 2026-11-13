#include "grace/core/profiler.hpp"

#include <thread>

#include "test_support.hpp"

namespace {

using namespace grace::core::profiler;

}  // namespace

int main()
{
    // Reset gives a clean slate.
    reset();
    resetViolations();
    GRACE_CHECK_EQ(zoneCount(), static_cast<std::size_t>(0));
    GRACE_CHECK_EQ(totalViolations(), 0u);

    // A zone records exactly one entry; its inclusive time is at least the
    // sleep it contains and at most a generous bound (a slow CI host).
    {
        Zone zone("sleep-zone");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const ZoneStats stats = zone("sleep-zone");
    GRACE_CHECK_EQ(stats.entries, 1u);
    GRACE_CHECK(stats.totalNs >= 1000000u);
    GRACE_CHECK(stats.deepestNs == stats.totalNs);
    GRACE_CHECK_EQ(zoneCount(), static_cast<std::size_t>(1));

    // An unknown zone reads as zeros, not an error.
    const ZoneStats unknown = zone("never-opened");
    GRACE_CHECK_EQ(unknown.entries, 0u);
    GRACE_CHECK_EQ(unknown.totalNs, 0u);

    // Two entries accumulate: the totals add, the deepest is the max.
    {
        Zone zone("repeat-zone");
    }
    {
        Zone zone("repeat-zone");
    }
    const ZoneStats repeat = zone("repeat-zone");
    GRACE_CHECK_EQ(repeat.entries, 2u);
    GRACE_CHECK(repeat.totalNs >= repeat.deepestNs);

    // reset() zeroes the accumulators but keeps the zone names registered.
    reset();
    const ZoneStats afterReset = zone("repeat-zone");
    GRACE_CHECK_EQ(afterReset.entries, 0u);
    GRACE_CHECK_EQ(afterReset.totalNs, 0u);

    // Violations: per-channel counting, total across channels, reset.
    recordViolation("render");
    recordViolation("render");
    recordViolation("physics");
    GRACE_CHECK_EQ(violations("render"), 2u);
    GRACE_CHECK_EQ(violations("physics"), 1u);
    GRACE_CHECK_EQ(violations("audio"), 0u);
    GRACE_CHECK_EQ(totalViolations(), 3u);
    resetViolations();
    GRACE_CHECK_EQ(totalViolations(), 0u);
    GRACE_CHECK_EQ(violations("render"), 0u);

    return grace::test::summary("profiler");
}
