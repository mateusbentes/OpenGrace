// Deterministic tests for the physics contract (8a) and the null backend
// (8b). No physics SDK: every check runs against the null backend's manual
// integration, which is reproducible bit-for-bit.
#include "contract_checks.hpp"

#include <cmath>
#include <memory>
#include <string>

#include "grace/physics/null_physics_backend.hpp"

namespace {

using grace::physics::BodyDesc;
using grace::physics::BodyKind;
using grace::physics::BuoyancyVolumeDesc;
using grace::physics::ForceFieldDesc;
using grace::physics::JointDesc;
using grace::physics::Body;
using grace::physics::Joint;
using grace::physics::NullPhysicsBackend;
using grace::physics::World;
using grace::physics::ShapeDesc;
using grace::physics::WorldConfig;
using grace::core::vec3;

int checks = 0;
int failures = 0;

void check(bool condition, const char* what)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fputs("FAIL: ", stdout);
        std::fputs(what, stdout);
        std::fputc('\n', stdout);
    }
}

bool near(float a, float b, float tol)
{
    return std::fabs(a - b) <= tol;
}

void testBackendContract(NullPhysicsBackend& backend)
{
    check(backend.describe() == "null (headless, deterministic manual integration)",
          "describe");
    check(backend.capabilityFlags() == 0u, "null backend has no extra flags");
    check(backend.violationCount() == 0u, "no violations yet");
}

void testWorldConfigValidation(NullPhysicsBackend&)
{
    WorldConfig ok;
    std::string message;
    check(ok.validate(message), "default world config is valid");
    (void)message;

    WorldConfig badStep;
    badStep.fixedStepSeconds = 0.0f;
    check(!badStep.validate(message), "zero step is invalid");
    check(!message.empty(), "message is filled");

    WorldConfig badGravity;
    badGravity.gravity = vec3{0.0f, std::nanf(""), 0.0f};
    check(!badGravity.validate(message), "NaN gravity is invalid");
}

void testBodyValidation(NullPhysicsBackend& backend)
{
    // WorldConfig check: createWorld throws on invalid config.
    WorldConfig bad;
    bad.fixedStepSeconds = -1.0f;
    bool threw = false;
    try
    {
        (void)backend.createWorld(bad);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "createWorld throws on invalid config");
}

void testFreeFallIntegration(NullPhysicsBackend& backend)
{
    WorldConfig config;
    config.fixedStepSeconds = 1.0f / 60.0f;
    World* world = backend.createWorld(config);

    BodyDesc desc;
    desc.kind = BodyKind::Dynamic;
    desc.shape.kind = ShapeDesc::Kind::Sphere;
    desc.shape.sphereRadius = 0.5f;
    desc.massKg = 1.0f;
    desc.affectedByGravity = true;
    Body* body = backend.createBody(world, desc);

    // Semi-implicit Euler: v = g*dt, x = x + v*dt.
    backend.step(world);
    auto state = backend.readState(body);
    const float dt = config.fixedStepSeconds;
    const float g = 9.81f;
    check(near(state.linearVelocity.y, -g * dt, 1e-6f), "step 1 velocity");
    check(near(state.position.y, -g * dt * dt, 1e-6f), "step 1 position");

    backend.step(world);
    state = backend.readState(body);
    check(near(state.linearVelocity.y, -2.0f * g * dt, 1e-6f),
          "step 2 velocity accumulates");
    check(near(state.position.y, -3.0f * g * dt * dt, 1e-6f),
          "step 2 position accumulates");

    // Static bodies are unaffected.
    BodyDesc staticDesc;
    staticDesc.kind = BodyKind::Static;
    staticDesc.shape.kind = ShapeDesc::Kind::Box;
    staticDesc.position = vec3{0.0f, 5.0f, 0.0f};
    Body* staticBody = backend.createBody(world, staticDesc);
    backend.step(world);
    auto staticState = backend.readState(staticBody);
    check(near(staticState.position.y, 5.0f, 1e-6f), "static body does not fall");

    // A body with affectedByGravity=false ignores gravity.
    BodyDesc noGravityDesc;
    noGravityDesc.kind = BodyKind::Dynamic;
    noGravityDesc.shape.kind = ShapeDesc::Kind::Sphere;
    noGravityDesc.shape.sphereRadius = 0.5f;
    noGravityDesc.affectedByGravity = false;
    Body* noGravity = backend.createBody(world, noGravityDesc);
    backend.step(world);
    auto noGravityState = backend.readState(noGravity);
    check(near(noGravityState.position.y, 0.0f, 1e-6f),
          "no-gravity body does not fall");

    backend.releaseWorld(world);
}

void testForcesAndImpulses(NullPhysicsBackend& backend)
{
    WorldConfig config;
    config.gravity = vec3{};
    config.fixedStepSeconds = 0.5f;
    World* world = backend.createWorld(config);

    BodyDesc desc;
    desc.kind = BodyKind::Dynamic;
    desc.shape.kind = ShapeDesc::Kind::Box;
    desc.massKg = 2.0f;
    desc.affectedByGravity = false;
    Body* body = backend.createBody(world, desc);

    // A force of 4 N on a 2 kg body for 0.5 s: v = 1 m/s.
    backend.applyForce(body, vec3{4.0f, 0.0f, 0.0f});
    backend.step(world);
    auto state = backend.readState(body);
    check(near(state.linearVelocity.x, 1.0f, 1e-6f), "force integrates");
    check(near(state.position.x, 0.5f, 1e-6f), "position integrates");

    // Forces are cleared after each step: the next step does not move.
    backend.step(world);
    state = backend.readState(body);
    check(near(state.linearVelocity.x, 1.0f, 1e-6f), "forces are cleared");
    check(near(state.position.x, 1.0f, 1e-6f), "position unchanged by force");

    // An impulse is a step change in momentum: 4 kg m/s on 2 kg = 2 m/s.
    backend.applyImpulse(body, vec3{4.0f, 0.0f, 0.0f});
    backend.step(world);
    state = backend.readState(body);
    check(near(state.linearVelocity.x, 3.0f, 1e-6f), "impulse changes velocity");
    check(near(state.position.x, 2.5f, 1e-6f), "impulse moves position");

    // Impulses are cleared too.
    backend.step(world);
    state = backend.readState(body);
    check(near(state.linearVelocity.x, 3.0f, 1e-6f), "impulses are cleared");

    backend.releaseWorld(world);
}

void testJoints(NullPhysicsBackend& backend)
{
    WorldConfig config;
    config.gravity = vec3{};
    World* world = backend.createWorld(config);

    BodyDesc desc;
    desc.kind = BodyKind::Dynamic;
    desc.shape.kind = ShapeDesc::Kind::Sphere;
    desc.shape.sphereRadius = 0.5f;
    (void)backend.createBody(world, desc);
    (void)backend.createBody(world, desc);

    JointDesc joint;
    joint.kind = JointDesc::Kind::Distance;
    joint.bodyA = 0;
    joint.bodyB = 1;
    joint.restLength = 2.0f;
    Joint* j = backend.createJoint(world, joint);
    check(j != nullptr, "joint is created");

    JointDesc badJoint;
    badJoint.bodyA = 0;
    badJoint.bodyB = 9;
    bool threw = false;
    try
    {
        (void)backend.createJoint(world, badJoint);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "unknown body index throws");

    backend.releaseJoint(world, j);
    backend.releaseWorld(world);
}

void testBuoyancy(NullPhysicsBackend& backend)
{
    WorldConfig config;
    config.gravity = vec3{0.0f, -10.0f, 0.0f};
    World* world = backend.createWorld(config);

    BodyDesc desc;
    desc.kind = BodyKind::Dynamic;
    desc.shape.kind = ShapeDesc::Kind::Box;
    desc.massKg = 1.0f;
    Body* body = backend.createBody(world, desc);

    // A fluid box that exactly fills the displacement: the body floats.
    BuoyancyVolumeDesc fluid;
    fluid.centre = vec3{0.0f, 0.0f, 0.0f};
    fluid.extents = vec3{1.0f, 1.0f, 1.0f};
    fluid.densityKgPerM3 = 1.0f;
    (void)backend.createBuoyancyVolume(world, fluid);

    backend.step(world);
    auto state = backend.readState(body);
    // Upward force = 1 m^3 * 1 kg/m^3 * 9.81 m/s^2; gravity = 10 N down.
    // Net acceleration = -0.19 m/s^2.
    check(near(state.linearVelocity.y, (-10.0f + 9.81f) * config.fixedStepSeconds,
               1e-5f),
          "buoyancy opposes gravity");
    check(body != nullptr, "body is present");

    backend.releaseWorld(world);
}

void testForceFields(NullPhysicsBackend& backend)
{
    WorldConfig config;
    config.gravity = vec3{};
    World* world = backend.createWorld(config);

    BodyDesc desc;
    desc.kind = BodyKind::Dynamic;
    desc.shape.kind = ShapeDesc::Kind::Sphere;
    desc.shape.sphereRadius = 0.5f;
    Body* inside = backend.createBody(world, desc);

    BodyDesc outsideDesc;
    outsideDesc.kind = BodyKind::Dynamic;
    outsideDesc.shape.kind = ShapeDesc::Kind::Sphere;
    outsideDesc.shape.sphereRadius = 0.5f;
    outsideDesc.position = vec3{10.0f, 0.0f, 0.0f};
    Body* outside = backend.createBody(world, outsideDesc);

    ForceFieldDesc field;
    field.centre = vec3{};
    field.extents = vec3{1.0f, 1.0f, 1.0f};
    field.acceleration = vec3{2.0f, 0.0f, 0.0f};
    (void)backend.createForceField(world, field);

    backend.step(world);
    const float dt = config.fixedStepSeconds;
    check(near(backend.readState(inside).linearVelocity.x, 2.0f * dt, 1e-6f),
          "field accelerates the body inside");
    check(near(backend.readState(outside).linearVelocity.x, 0.0f, 1e-6f),
          "field does not affect the body outside");

    backend.releaseWorld(world);
}

void testProtocolViolations(NullPhysicsBackend& backend)
{
    const std::uint64_t before = backend.violationCount();
    WorldConfig config;
    World* world = backend.createWorld(config);
    backend.releaseWorld(world);
    // Release twice: the second is a violation.
    backend.releaseWorld(world);
    check(backend.violationCount() == before + 1u,
          "double release is a violation");

    // Shutdown twice: the second is a violation.
    backend.shutdown();
    check(backend.violationCount() == before + 1u, "first shutdown is clean");
    backend.shutdown();
    check(backend.violationCount() == before + 2u,
          "second shutdown is a violation");
}

void testBodyDescValidation(NullPhysicsBackend& backend)
{
    WorldConfig config;
    World* world = backend.createWorld(config);

    BodyDesc bad;
    bad.kind = BodyKind::Dynamic;
    bad.massKg = 0.0f;
    bool threw = false;
    try
    {
        (void)backend.createBody(world, bad);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "zero-mass dynamic body throws");

    BodyDesc badShape;
    badShape.kind = BodyKind::Static;
    badShape.shape.kind = ShapeDesc::Kind::Sphere;
    badShape.shape.sphereRadius = -1.0f;
    threw = false;
    try
    {
        (void)backend.createBody(world, badShape);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "negative-radius sphere throws");

    backend.releaseWorld(world);
}

}  // namespace

int main()
{
    NullPhysicsBackend backend;

    testBackendContract(backend);
    testWorldConfigValidation(backend);
    testBodyValidation(backend);
    testFreeFallIntegration(backend);
    testForcesAndImpulses(backend);
    testJoints(backend);
    testBuoyancy(backend);
    testForceFields(backend);
    testProtocolViolations(backend);
    testBodyDescValidation(backend);

    std::printf("physics: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
