#pragma once
// The game module: the agent behaviour system and the gameplay-level
// services (docs/grace-architecture.md section 2's module table).
//
// The module is plain data, deliberately, the scene module's pattern:
// the agents' steering, the timers and the event queue are
// deterministic CPU work driven by the application's fixed step, so
// the same inputs and the same seed produce the same outputs every
// run. The random source is a xorshift64* generator seeded explicitly;
// no library random state exists. Nothing here names a graphics API, a
// physics SDK or a scripting language.
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "grace/game/game_types.hpp"
namespace grace {
namespace game {
/// The game module's error: the throws the services report. The
/// message is the module's own words, and the failure is a build
/// error, not a runtime fallback.
class GameError : public std::runtime_error
{
public:
    explicit GameError(const std::string& message);
};
/// The game services: the agents, the timers, the properties, the
/// event queue and the deterministic random source. The operations
/// are deterministic.
class GameServices
{
public:
    GameServices();
    ~GameServices();
    GameServices(const GameServices&) = delete;
    GameServices& operator=(const GameServices&) = delete;
    // -- Agents -----------------------------------------
    //
    /// Adds an agent. Throws GameError on an invalid description (a
    /// non-empty name, a non-negative speed, a patrol with fewer than
    /// two waypoints) or a duplicate name.
    std::uint32_t addAgent(const AgentDesc& desc);
    /// The agent count.
    std::size_t agentCount() const;
    /// The agent's state by id. Throws GameError on an unknown id.
    const AgentState& agent(std::uint32_t agentId) const;
    /// Sets an agent's behaviour and its waypoints. Throws GameError
    /// on an unknown id or a patrol with fewer than two waypoints.
    void setBehaviour(std::uint32_t agentId, BehaviourKind kind,
                      const std::vector<core::vec3>& waypoints);
    /// Sets an agent's target: the point the seek and flee behaviours
    /// move toward and away from.
    void setTarget(const core::vec3& target);
    /// The target point.
    const core::vec3& target() const;
    // -- Timers -----------------------------------------
    //
    /// Adds a timer. Throws GameError on an invalid description (a
    /// non-empty name, a positive duration) or a duplicate name.
    std::uint32_t addTimer(const TimerDesc& desc);
    /// The timer count.
    std::size_t timerCount() const;
    /// The timer's state by id. Throws GameError on an unknown id.
    const TimerState& timer(std::uint32_t timerId) const;
    // -- Properties -------------------------------------
    //
    /// Sets a property. The kind is fixed by the first set; a second
    /// set with a different kind throws GameError.
    void setProperty(const Property& property);
    /// The property by name. Throws GameError on an unknown name.
    const Property& property(const std::string& name) const;
    /// The property count.
    std::size_t propertyCount() const;
    // -- Events -----------------------------------------
    //
    /// Pushes an event onto the queue.
    void pushEvent(const GameEvent& event);
    /// Drains the queue into `out`, oldest first, and clears it.
    void drainEvents(std::vector<GameEvent>& out);
    /// The pending event count.
    std::size_t eventCount() const;
    // -- Random -----------------------------------------
    //
    /// Seeds the random source. The same seed produces the same
    /// sequence.
    void seed(std::uint64_t seed);
    /// Draws the next number in [0, 0xFFFFFFFF].
    std::uint32_t randomU32();
    /// Draws the next number in [0, 1) as a float.
    float randomFloat();
    // -- Update -----------------------------------------
    //
    /// Advances one fixed step: the agents steer (the patrol walks its
    /// waypoints in order and wraps, the seek moves toward the target
    /// and stops at it, the flee moves away), the timers tick and fire
    /// their events onto the queue ("timer:<name>" with the fire count
    /// as the payload).
    void update(float deltaTime);
    /// The module's statistics.
    GameStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace game
}  // namespace grace
