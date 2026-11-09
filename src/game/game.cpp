// The game module's implementation: the agent behaviour system and the
// gameplay-level services.
//
// The steering is deterministic CPU work driven by the application's
// fixed step: the patrol walks its waypoints in order and wraps, the
// seek moves toward the target and stops at it, the flee moves away.
// The random source is a xorshift64* generator the application seeds
// explicitly, so the same seed produces the same sequence every run.
#include "grace/game/game.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace grace {
namespace game {
GameError::GameError(const std::string& message)
    : std::runtime_error(message)
{
}
namespace {
/// The xorshift64* step: one multiply-xorshift pass. The zero state
/// stays zero, which the seed validation rejects.
std::uint64_t xorshift64star(std::uint64_t& state)
{
    state ^= state >> 12u;
    state ^= state << 25u;
    state ^= state >> 27u;
    return state * 2685821657736338717ull;
}
}  // namespace
struct GameServices::Impl
{
    std::map<std::uint32_t, AgentState> agents;
    std::map<std::uint32_t, TimerState> timers;
    std::map<std::string, Property> properties;
    std::vector<GameEvent> events;
    std::set<std::string> agentNames;
    std::set<std::string> timerNames;
    std::uint32_t nextAgentId = 1;
    std::uint32_t nextTimerId = 1;
    core::vec3 targetPoint{0.0f, 0.0f, 0.0f};
    std::uint64_t randomState = 0;
    std::uint64_t randomCount = 0;
    /// Steps one agent toward the point by the distance the speed and
    /// the delta time give, never overshooting.
    static void stepToward(AgentState& agent, const core::vec3& to,
                           float distance)
    {
        const core::vec3 delta = to - agent.desc.position;
        const float span = core::length(delta);
        if (span <= 1e-6f || distance <= 0.0f)
        {
            return;
        }
        const float move = std::min(distance, span);
        const core::vec3 dir = delta / span;
        agent.direction = dir;
        agent.desc.position = agent.desc.position + dir * move;
    }
};
GameServices::GameServices() : impl(new Impl)
{
}
GameServices::~GameServices() = default;
std::uint32_t GameServices::addAgent(const AgentDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
    {
        throw GameError(message);
    }
    if (impl->agentNames.count(desc.name))
    {
        throw GameError("a duplicate agent name");
    }
    const std::uint32_t id = impl->nextAgentId++;
    AgentState state;
    state.id = id;
    state.desc = desc;
    impl->agents[id] = state;
    impl->agentNames.insert(desc.name);
    return id;
}
std::size_t GameServices::agentCount() const
{
    return impl->agents.size();
}
const AgentState& GameServices::agent(std::uint32_t agentId) const
{
    const auto it = impl->agents.find(agentId);
    if (it == impl->agents.end())
    {
        throw GameError("an agent id out of range");
    }
    return it->second;
}
void GameServices::setBehaviour(std::uint32_t agentId, BehaviourKind kind,
                                const std::vector<core::vec3>& waypoints)
{
    const auto it = impl->agents.find(agentId);
    if (it == impl->agents.end())
    {
        throw GameError("an agent id out of range");
    }
    if (kind == BehaviourKind::Patrol && waypoints.size() < 2u)
    {
        throw GameError("a patrol agent's waypoint list has fewer than "
                        "two waypoints");
    }
    it->second.desc.behaviour = kind;
    it->second.desc.waypoints = waypoints;
    it->second.waypointIndex = 0;
}
void GameServices::setTarget(const core::vec3& target)
{
    impl->targetPoint = target;
}
const core::vec3& GameServices::target() const
{
    return impl->targetPoint;
}
std::uint32_t GameServices::addTimer(const TimerDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
    {
        throw GameError(message);
    }
    if (impl->timerNames.count(desc.name))
    {
        throw GameError("a duplicate timer name");
    }
    const std::uint32_t id = impl->nextTimerId++;
    TimerState state;
    state.desc = desc;
    state.remaining = desc.duration;
    impl->timers[id] = state;
    impl->timerNames.insert(desc.name);
    return id;
}
std::size_t GameServices::timerCount() const
{
    return impl->timers.size();
}
const TimerState& GameServices::timer(std::uint32_t timerId) const
{
    const auto it = impl->timers.find(timerId);
    if (it == impl->timers.end())
    {
        throw GameError("a timer id out of range");
    }
    return it->second;
}
void GameServices::setProperty(const Property& property)
{
    if (property.name.empty())
    {
        throw GameError("a property's name is empty");
    }
    const auto it = impl->properties.find(property.name);
    if (it != impl->properties.end() && it->second.kind != property.kind)
    {
        throw GameError("a property's kind changes");
    }
    impl->properties[property.name] = property;
}
const Property& GameServices::property(const std::string& name) const
{
    const auto it = impl->properties.find(name);
    if (it == impl->properties.end())
    {
        throw GameError("a property's name is unknown");
    }
    return it->second;
}
std::size_t GameServices::propertyCount() const
{
    return impl->properties.size();
}
void GameServices::pushEvent(const GameEvent& event)
{
    impl->events.push_back(event);
}
void GameServices::drainEvents(std::vector<GameEvent>& out)
{
    out.swap(impl->events);
    impl->events.clear();
}
std::size_t GameServices::eventCount() const
{
    return impl->events.size();
}
void GameServices::seed(std::uint64_t seed)
{
    if (seed == 0u)
    {
        throw GameError("a zero seed");
    }
    impl->randomState = seed;
    impl->randomCount = 0;
}
std::uint32_t GameServices::randomU32()
{
    if (impl->randomState == 0u)
    {
        throw GameError("the random source is not seeded");
    }
    ++impl->randomCount;
    return static_cast<std::uint32_t>(
        xorshift64star(impl->randomState) >> 32u);
}
float GameServices::randomFloat()
{
    const std::uint32_t value = randomU32();
    return static_cast<float>(value) * (1.0f / 4294967296.0f);
}
void GameServices::update(float deltaTime)
{
    if (deltaTime < 0.0f)
    {
        throw GameError("a negative delta time");
    }
    // The agents steer.
    for (auto& entry : impl->agents)
    {
        AgentState& agent = entry.second;
        const float distance = agent.desc.speed * deltaTime;
        switch (agent.desc.behaviour)
        {
        case BehaviourKind::Idle:
            break;
        case BehaviourKind::Patrol:
        {
            if (agent.desc.waypoints.empty())
            {
                break;
            }
            const core::vec3& waypoint =
                agent.desc.waypoints[agent.waypointIndex];
            Impl::stepToward(agent, waypoint, distance);
            if (core::length(agent.desc.position - waypoint) <= 1e-6f)
            {
                agent.waypointIndex =
                    (agent.waypointIndex + 1u)
                    % agent.desc.waypoints.size();
            }
            break;
        }
        case BehaviourKind::Seek:
            Impl::stepToward(agent, impl->targetPoint, distance);
            break;
        case BehaviourKind::Flee:
        {
            const core::vec3 away =
                agent.desc.position
                + (agent.desc.position - impl->targetPoint);
            Impl::stepToward(agent, away, distance);
            break;
        }
        }
    }
    // The timers tick and fire.
    for (auto& entry : impl->timers)
    {
        TimerState& timer = entry.second;
        timer.remaining -= deltaTime;
        while (timer.remaining <= 0.0f)
        {
            ++timer.fireCount;
            GameEvent event;
            event.name = "timer:" + timer.desc.name;
            event.payload = std::to_string(timer.fireCount);
            impl->events.push_back(event);
            if (timer.desc.repeating)
            {
                timer.remaining += timer.desc.duration;
            }
            else
            {
                timer.remaining = 0.0f;
                break;
            }
        }
    }
}
GameStats GameServices::stats() const
{
    GameStats stats;
    stats.agentCount = static_cast<std::uint32_t>(impl->agents.size());
    stats.timerCount = static_cast<std::uint32_t>(impl->timers.size());
    stats.eventCount = static_cast<std::uint32_t>(impl->events.size());
    stats.randomCount = impl->randomCount;
    return stats;
}
}  // namespace game
}  // namespace grace
