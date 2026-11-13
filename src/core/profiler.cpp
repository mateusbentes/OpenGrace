#include "grace/core/profiler.hpp"

#include <chrono>
#include <unordered_map>

namespace grace::core::profiler {
namespace {

/// The steady clock's nanoseconds now.
std::uint64_t nowNs() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct Storage
{
    std::unordered_map<std::string, ZoneStats> zones;
    std::unordered_map<std::string, std::uint64_t> violations;
};

Storage& storage()
{
    static Storage instance;
    return instance;
}

}  // namespace

void reset()
{
    for (auto& entry : storage().zones)
    {
        entry.second.totalNs = 0;
        entry.second.entries = 0;
        entry.second.deepestNs = 0;
    }
}

ZoneStats zone(const std::string& name)
{
    const auto& zones = storage().zones;
    const auto it = zones.find(name);
    return it == zones.end() ? ZoneStats{} : it->second;
}

std::size_t zoneCount()
{
    return storage().zones.size();
}

Zone::Zone(const char* name) : m_name(name), m_startNs(nowNs()) {}

Zone::~Zone()
{
    const std::uint64_t duration = nowNs() - m_startNs;
    ZoneStats& stats = storage().zones[m_name];
    stats.totalNs += duration;
    ++stats.entries;
    if (duration > stats.deepestNs)
        stats.deepestNs = duration;
}

void recordViolation(const char* channel)
{
    ++storage().violations[channel];
}

std::uint64_t violations(const char* channel)
{
    const auto& map = storage().violations;
    const auto it = map.find(channel);
    return it == map.end() ? 0u : it->second;
}

std::uint64_t totalViolations()
{
    std::uint64_t total = 0;
    for (const auto& entry : storage().violations)
        total += entry.second;
    return total;
}

void resetViolations()
{
    storage().violations.clear();
}

}  // namespace grace::core::profiler
