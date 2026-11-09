// The game suite: the agent behaviour system and the gameplay-level
// services (15). Every check runs on the deterministic CPU evaluation,
// which is reproducible bit-for-bit: the same seed produces the same
// random sequence, the same inputs produce the same steering.
#include "contract_checks.hpp"
#include <cmath>
#include <string>
#include <vector>
#include "grace/game/game.hpp"
namespace {
using grace::core::vec3;
using grace::game::AgentDesc;
using grace::game::AgentState;
using grace::game::BehaviourKind;
using grace::game::GameError;
using grace::game::GameEvent;
using grace::game::GameServices;
using grace::game::GameStats;
using grace::game::Property;
using grace::game::PropertyKind;
using grace::game::TimerDesc;
using grace::game::TimerState;
void testAgentValidation()
{
    std::string message;
    AgentDesc desc;
    desc.name = "guard";
    desc.behaviour = BehaviourKind::Idle;
    desc.speed = 1.0f;
    GRACE_CHECK(desc.validate(message));
    desc.name.clear();
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message == "an agent's name is empty");
    desc.name = "guard";
    desc.speed = -1.0f;
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message == "an agent's speed is negative");
    desc.speed = 1.0f;
    desc.behaviour = BehaviourKind::Patrol;
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message.find("fewer than two") != std::string::npos);
    desc.waypoints = {vec3{0.0f, 0.0f, 0.0f}, vec3{1.0f, 0.0f, 0.0f}};
    GRACE_CHECK(desc.validate(message));
}
void testTimerValidation()
{
    std::string message;
    TimerDesc desc;
    desc.name = "respawn";
    desc.duration = 2.0f;
    GRACE_CHECK(desc.validate(message));
    desc.name.clear();
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message == "a timer's name is empty");
    desc.name = "respawn";
    desc.duration = 0.0f;
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message == "a timer's duration is not positive");
}
void testAgentLifecycle()
{
    GameServices services;
    AgentDesc desc;
    desc.name = "guard";
    desc.speed = 2.0f;
    bool threw = false;
    try
    {
        services.addAgent(desc);
    }
    catch (const GameError&)
    {
        threw = true;
    }
    GRACE_CHECK(!threw);
    GRACE_CHECK(services.agentCount() == 1u);
    // A duplicate name throws.
    threw = false;
    try
    {
        services.addAgent(desc);
    }
    catch (const GameError& error)
    {
        threw = true;
        GRACE_CHECK(std::string(error.what())
                    == "a duplicate agent name");
    }
    GRACE_CHECK(threw);
    // An unknown id throws.
    threw = false;
    try
    {
        services.agent(99u);
    }
    catch (const GameError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
}
void testIdleAndSeek()
{
    GameServices services;
    AgentDesc idle;
    idle.name = "idle";
    idle.behaviour = BehaviourKind::Idle;
    idle.position = vec3{1.0f, 0.0f, 0.0f};
    idle.speed = 2.0f;
    const std::uint32_t idleId = services.addAgent(idle);
    (void)idleId;
    AgentDesc seeker;
    seeker.name = "seeker";
    seeker.behaviour = BehaviourKind::Seek;
    seeker.position = vec3{0.0f, 0.0f, 0.0f};
    seeker.speed = 1.0f;
    const std::uint32_t seekId = services.addAgent(seeker);
    (void)seekId;
    services.setTarget(vec3{10.0f, 0.0f, 0.0f});
    // The idle agent holds its position; the seek agent moves toward
    // the target by speed * dt.
    services.update(0.5f);
    GRACE_CHECK((services.agent(idleId).desc.position
                == vec3{1.0f, 0.0f, 0.0f}));
    const AgentState& moved = services.agent(seekId);
    GRACE_CHECK(std::abs(moved.desc.position.x - 0.5f) < 1e-6f);
    // The direction record points at the target.
    GRACE_CHECK(std::abs(moved.direction.x - 1.0f) < 1e-6f);
    // The seek stops at the target: it never overshoots.
    services.update(100.0f);
    GRACE_CHECK((services.agent(seekId).desc.position
                == vec3{10.0f, 0.0f, 0.0f}));
}
void testPatrol()
{
    GameServices services;
    AgentDesc patroller;
    patroller.name = "patrol";
    patroller.behaviour = BehaviourKind::Patrol;
    patroller.speed = 1.0f;
    patroller.waypoints = {vec3{0.0f, 0.0f, 0.0f},
                           vec3{2.0f, 0.0f, 0.0f}};
    patroller.position = vec3{0.0f, 0.0f, 0.0f};
    const std::uint32_t id = services.addAgent(patroller);
    // The agent walks to the first waypoint (already there), then the
    // index advances without a move; it walks toward the second,
    // arrives, and wraps back to the first.
    services.update(1.0f);
    GRACE_CHECK(services.agent(id).waypointIndex == 1u);
    GRACE_CHECK(std::abs(services.agent(id).desc.position.x)
                < 1e-6f);
    // The walk to the second waypoint takes two seconds.
    services.update(1.0f);
    GRACE_CHECK(services.agent(id).waypointIndex == 1u);
    GRACE_CHECK(std::abs(services.agent(id).desc.position.x - 1.0f)
                < 1e-6f);
    services.update(2.0f);
    // The second waypoint reached: the index wraps to the first.
    GRACE_CHECK(services.agent(id).waypointIndex == 0u);
    GRACE_CHECK(std::abs(services.agent(id).desc.position.x - 2.0f)
                < 1e-6f);
    // The walk home: one more step lands the agent back at the first
    // waypoint.
    services.update(2.0f);
    GRACE_CHECK(std::abs(services.agent(id).desc.position.x)
                < 1e-6f);
}
void testFlee()
{
    GameServices services;
    AgentDesc fleer;
    fleer.name = "flee";
    fleer.behaviour = BehaviourKind::Flee;
    fleer.speed = 1.0f;
    fleer.position = vec3{1.0f, 0.0f, 0.0f};
    const std::uint32_t id = services.addAgent(fleer);
    services.setTarget(vec3{0.0f, 0.0f, 0.0f});
    // The flee moves away from the target: the agent's x grows.
    services.update(1.0f);
    GRACE_CHECK(services.agent(id).desc.position.x > 1.9f);
}
void testTimers()
{
    GameServices services;
    TimerDesc once;
    once.name = "once";
    once.duration = 1.0f;
    const std::uint32_t onceId = services.addTimer(once);
    TimerDesc repeat;
    repeat.name = "tick";
    repeat.duration = 0.5f;
    repeat.repeating = true;
    const std::uint32_t repeatId = services.addTimer(repeat);
    // A duplicate name throws.
    bool threw = false;
    try
    {
        services.addTimer(once);
    }
    catch (const GameError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
    // After 1.1 seconds the once timer fired once and stopped; the
    // repeating timer fired twice.
    services.update(1.1f);
    GRACE_CHECK(services.timer(onceId).fireCount == 1u);
    GRACE_CHECK(services.timer(onceId).remaining == 0.0f);
    GRACE_CHECK(services.timer(repeatId).fireCount == 2u);
    // The timer events landed on the queue with the fire count as the
    // payload.
    std::vector<GameEvent> drained;
    services.drainEvents(drained);
    GRACE_CHECK(drained.size() == 3u);
    GRACE_CHECK(drained[0].name == "timer:once");
    GRACE_CHECK(drained[0].payload == "1");
    GRACE_CHECK(drained[1].name == "timer:tick");
    GRACE_CHECK(drained[2].name == "timer:tick");
    GRACE_CHECK(drained[2].payload == "2");
    // The queue is empty after the drain.
    GRACE_CHECK(services.eventCount() == 0u);
}
void testProperties()
{
    GameServices services;
    Property hits;
    hits.name = "hits";
    hits.kind = PropertyKind::Int;
    hits.intValue = 3;
    services.setProperty(hits);
    GRACE_CHECK(services.propertyCount() == 1u);
    GRACE_CHECK(services.property("hits").intValue == 3);
    // The same kind updates in place.
    hits.intValue = 5;
    services.setProperty(hits);
    GRACE_CHECK(services.property("hits").intValue == 5);
    // A different kind on the same name throws.
    Property floatHits = hits;
    floatHits.kind = PropertyKind::Float;
    bool threw = false;
    try
    {
        services.setProperty(floatHits);
    }
    catch (const GameError& error)
    {
        threw = true;
        GRACE_CHECK(std::string(error.what())
                    == "a property's kind changes");
    }
    GRACE_CHECK(threw);
    // An unknown name throws.
    threw = false;
    try
    {
        services.property("missing");
    }
    catch (const GameError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
    // A string property carries UTF-8 bytes.
    Property greeting;
    greeting.name = "greeting";
    greeting.kind = PropertyKind::String;
    greeting.stringValue = "h\xC3\xA9llo";
    services.setProperty(greeting);
    GRACE_CHECK(services.property("greeting").stringValue
                == "h\xC3\xA9llo");
}
void testEvents()
{
    GameServices services;
    // The queue keeps the push order; the drain swaps and clears.
    services.pushEvent(GameEvent{"a", ""});
    services.pushEvent(GameEvent{"b", "payload"});
    GRACE_CHECK(services.eventCount() == 2u);
    std::vector<GameEvent> drained;
    services.drainEvents(drained);
    GRACE_CHECK(drained.size() == 2u);
    GRACE_CHECK(drained[0].name == "a");
    GRACE_CHECK(drained[1].name == "b");
    GRACE_CHECK(services.eventCount() == 0u);
}
void testRandom()
{
    GameServices services;
    // An unseeded source throws.
    bool threw = false;
    try
    {
        services.randomU32();
    }
    catch (const GameError& error)
    {
        threw = true;
        GRACE_CHECK(std::string(error.what())
                    == "the random source is not seeded");
    }
    GRACE_CHECK(threw);
    // A zero seed throws.
    threw = false;
    try
    {
        services.seed(0u);
    }
    catch (const GameError& error)
    {
        threw = true;
        GRACE_CHECK(std::string(error.what()) == "a zero seed");
    }
    GRACE_CHECK(threw);
    // The same seed produces the same sequence; a different seed
    // produces a different one.
    services.seed(42u);
    const std::uint32_t a1 = services.randomU32();
    const std::uint32_t a2 = services.randomU32();
    services.seed(42u);
    GRACE_CHECK(services.randomU32() == a1);
    GRACE_CHECK(services.randomU32() == a2);
    services.seed(43u);
    GRACE_CHECK(services.randomU32() != a1);
    // The float form stays in [0, 1).
    services.seed(7u);
    for (int i = 0; i < 64; ++i)
    {
        const float value = services.randomFloat();
        GRACE_CHECK(value >= 0.0f && value < 1.0f);
    }
    // The stats count the draws since the last seed.
    GRACE_CHECK(services.stats().randomCount == 64u);
}
void testStats()
{
    GameServices services;
    AgentDesc desc;
    desc.name = "a";
    const std::uint32_t agentId = services.addAgent(desc);
    (void)agentId;
    TimerDesc timer;
    timer.name = "t";
    timer.duration = 1.0f;
    const std::uint32_t timerId = services.addTimer(timer);
    (void)timerId;
    services.pushEvent(GameEvent{"e", ""});
    const GameStats stats = services.stats();
    GRACE_CHECK(stats.agentCount == 1u);
    GRACE_CHECK(stats.timerCount == 1u);
    GRACE_CHECK(stats.eventCount == 1u);
    GRACE_CHECK(stats.randomCount == 0u);
}
}  // namespace
int main()
{
    testAgentValidation();
    testTimerValidation();
    testAgentLifecycle();
    testIdleAndSeek();
    testPatrol();
    testFlee();
    testTimers();
    testProperties();
    testEvents();
    testRandom();
    testStats();
    return grace::test::summary("game");
}
