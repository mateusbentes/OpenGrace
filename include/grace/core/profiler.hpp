#pragma once

#include <cstdint>
#include <string>

namespace grace::core {

/// The built-in profiler (GRACE 2's documented "built-in profiler").
///
/// Zone-based: a scope opened with Zone runs its code with a named, timed
/// zone on top of the steady clock. The profiler owns no thread and never
/// blocks: record time goes into a per-zone accumulator that the host reads
/// between frames. The counters are plain 64-bit integers, so a zone on one
/// thread and a read on another sees either the old or the new total, never
/// a torn one (the documented tolerance for a lock-free profiler).
///
/// The validator (the documented "runtime data validator") is the second
/// half: recordViolation() counts contract violations per channel, and the
/// host decides between reporting and aborting. The engine's own math
/// contract routes into a handler, so the validator here is the counters
/// and the report, not a second violation mechanism.
namespace profiler {

/// One accumulated zone: total wall time, entry count, and the deepest
/// single entry. Times are nanoseconds of the steady clock.
struct ZoneStats
{
    std::uint64_t totalNs = 0;
    std::uint64_t entries = 0;
    std::uint64_t deepestNs = 0;
};

/// Resets every zone's accumulator. Called at a frame boundary so a
/// per-frame report is a diff of two resets, not a forever-growing total.
void reset();

/// The stats of one zone by name. An unknown name returns zeros (a host
/// may sample before the zone's first entry).
ZoneStats zone(const std::string& name);

/// The number of distinct zones recorded since the last reset.
std::size_t zoneCount();

/// RAII zone: opens on construction, closes on destruction. Nesting is
/// tracked per thread; the deepestNs stat records the single entry's
/// duration including nested children (the inclusive time the frame
/// reports want).
class Zone
{
public:
    explicit Zone(const char* name);
    ~Zone();
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;

private:
    const char* m_name;
    std::uint64_t m_startNs;
};

/// Counts one violation on a channel (for example "render", "physics").
/// Never throws, never blocks.
void recordViolation(const char* channel);

/// The violations counted on a channel so far (0 for an unknown channel).
std::uint64_t violations(const char* channel);

/// The total violations across all channels.
std::uint64_t totalViolations();

/// Resets the violation counters (a frame boundary call, like reset()).
void resetViolations();

}  // namespace profiler

}  // namespace grace::core
