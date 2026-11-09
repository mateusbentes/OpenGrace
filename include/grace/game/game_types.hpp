#pragma once
// The game module's data types: the agents, the behaviours and the
// gameplay-service descriptions (docs/grace-architecture.md section 2's
// module table: AI is agent behaviour, Game is gameplay-level services).
//
// The module is plain data, deliberately: the steering, the timers and
// the random source are deterministic CPU work, so the same inputs and
// the same seed produce the same outputs every run. Nothing here names
// a graphics API, a physics SDK or a scripting language.
#include <cstdint>
#include <string>
#include <vector>
#include "grace/core/math/vec3.hpp"
namespace grace {
namespace game {
/// The agent behaviours the built-in set covers: idle holds the
/// position, patrol walks the waypoint list, seek moves toward the
/// target, flee moves away from it.
enum class BehaviourKind : std::uint8_t
{
    Idle = 0,
    Patrol = 1,
    Seek = 2,
    Flee = 3
};
/// An agent's description: the name, the behaviour, the waypoints the
/// patrol walks in order, the movement speed in units per second and
/// the starting position.
struct AgentDesc final
{
    std::string name;
    BehaviourKind behaviour = BehaviourKind::Idle;
    std::vector<core::vec3> waypoints;
    float speed = 1.0f;
    core::vec3 position{0.0f, 0.0f, 0.0f};
    bool validate(std::string& message) const;
};
/// An agent's runtime state: the description's data plus the waypoint
/// index the patrol is walking toward and the last step's direction.
struct AgentState final
{
    std::uint32_t id = 0;
    AgentDesc desc;
    std::size_t waypointIndex = 0;
    core::vec3 direction{0.0f, 0.0f, 1.0f};
};
/// A timer's description: the name, the duration in seconds and
/// whether it repeats. A timer fires when its remaining time reaches
/// zero; a repeating timer restarts its duration.
struct TimerDesc final
{
    std::string name;
    float duration = 0.0f;
    bool repeating = false;
    bool validate(std::string& message) const;
};
/// A timer's runtime state: the description plus the remaining time
/// and the fire count.
struct TimerState final
{
    TimerDesc desc;
    float remaining = 0.0f;
    std::uint64_t fireCount = 0;
};
/// A named property's kind: the integer, the float and the string the
/// game state carries.
enum class PropertyKind : std::uint8_t
{
    Int = 0,
    Float = 1,
    String = 2
};
/// A named property: the name, the kind and the value in the kind's
/// own slot.
struct Property final
{
    std::string name;
    PropertyKind kind = PropertyKind::Int;
    std::int64_t intValue = 0;
    double floatValue = 0.0;
    std::string stringValue;
};
/// A gameplay event: the name and the optional payload string. The
/// event queue is the services' own; the application pushes and drains.
struct GameEvent final
{
    std::string name;
    std::string payload;
};
/// The game module's statistics: the agent count, the timer count,
/// the pending event count and the random numbers drawn.
struct GameStats final
{
    std::uint32_t agentCount = 0;
    std::uint32_t timerCount = 0;
    std::uint32_t eventCount = 0;
    std::uint64_t randomCount = 0;
};
}  // namespace game
}  // namespace grace
