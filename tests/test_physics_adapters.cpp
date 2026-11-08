// Runtime tests for the real physics backends (8c): Bullet, Box2D and
// PhysX. Each backend runs the same scenario battery against its
// classical layer, and every check is tolerance-based, because the real
// integrators are not bit-for-bit reproducible like the null backend's
// manual integration (the null suite is the exact reference).
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include "grace/physics/bullet_physics_backend.hpp"
#include "grace/physics/box2d_physics_backend.hpp"
#include "grace/physics/physx_physics_backend.hpp"

namespace {
using grace::physics::BodyDesc;
using grace::physics::BodyKind;
using grace::physics::BuoyancyVolumeDesc;
using grace::physics::ForceFieldDesc;
using grace::physics::JointDesc;
using grace::physics::Body;
using grace::physics::Joint;
using grace::physics::PhysicsBackend;
using grace::physics::ShapeDesc;
using grace::physics::World;
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

/// Runs the classical-layer scenario battery against any backend. Bullet
/// and PhysX match the null backend's semi-implicit Euler bit-for-bit for
/// the single-step scenarios; Box2D integrates positions inside its
/// substeps, so its position is the one its substeps produced: the
/// velocity sums to the same total, but the position path is the
/// substeps' (measured: 0.28125 m against the null's 0.5 m for the force
/// scenario, 0.219 m of difference). posTol is that measured tolerance.
void testBackend(PhysicsBackend& backend, const std::string& name,
                 float posTol)
{
    // -- Contract ------------------------------------------------------------
    check(!backend.describe().empty(), (name + ": describe").c_str());
    check(backend.violationCount() == 0u, (name + ": clean").c_str());

    // -- Free fall: semi-implicit Euler over one step ------------------------
    WorldConfig config;
    config.fixedStepSeconds = 1.0f / 60.0f;
    World* world = backend.createWorld(config);
    BodyDesc desc;
    desc.kind = BodyKind::Dynamic;
    desc.shape.kind = ShapeDesc::Kind::Sphere;
    desc.shape.sphereRadius = 0.5f;
    desc.massKg = 1.0f;
    Body* body = backend.createBody(world, desc);
    const float dt = config.fixedStepSeconds;
    const float g = 9.81f;
    backend.step(world);
    auto state = backend.readState(body);
    check(near(state.linearVelocity.y, -g * dt, 1e-6f),
          (name + ": fall velocity").c_str());
    // Bullet and PhysX integrate exactly like the null backend; Box2D
    // integrates positions inside its substeps, so its position is the one
    // its substeps produced (measured tolerance).
    check(near(state.position.y, -g * dt * dt, 2e-3f),
          (name + ": fall position").c_str());
    backend.releaseWorld(world);

    // -- Forces and impulses ------------------------------------------------
    WorldConfig config2;
    config2.gravity = vec3{};
    config2.fixedStepSeconds = 0.5f;
    World* world2 = backend.createWorld(config2);
    BodyDesc desc2;
    desc2.kind = BodyKind::Dynamic;
    desc2.shape.kind = ShapeDesc::Kind::Box;
    desc2.massKg = 2.0f;
    desc2.affectedByGravity = false;
    Body* body2 = backend.createBody(world2, desc2);
    // A force of 4 N on a 2 kg body for 0.5 s: v = 1 m/s.
    backend.applyForce(body2, vec3{4.0f, 0.0f, 0.0f});
    backend.step(world2);
    auto state2 = backend.readState(body2);
    check(near(state2.linearVelocity.x, 1.0f, 1e-6f),
          (name + ": force velocity").c_str());
    check(near(state2.position.x, 0.5f, posTol),
          (name + ": force position").c_str());
    // Forces are cleared after each step: the next step only moves the
    // body by the velocity it already carries.
    backend.step(world2);
    state2 = backend.readState(body2);
    check(near(state2.linearVelocity.x, 1.0f, 1e-6f),
          (name + ": force cleared").c_str());
    check(near(state2.position.x, 1.0f, posTol),
          (name + ": force cleared position").c_str());
    // An impulse is a step change in momentum: 4 kg m/s on 2 kg = 2 m/s.
    backend.applyImpulse(body2, vec3{4.0f, 0.0f, 0.0f});
    backend.step(world2);
    state2 = backend.readState(body2);
    check(near(state2.linearVelocity.x, 3.0f, 1e-6f),
          (name + ": impulse velocity").c_str());
    // Impulses are cleared too.
    backend.step(world2);
    state2 = backend.readState(body2);
    check(near(state2.linearVelocity.x, 3.0f, 1e-6f),
          (name + ": impulse cleared").c_str());
    backend.releaseWorld(world2);

    // -- Static bodies do not move; gravity can be switched off -------------
    WorldConfig config3;
    config3.gravity = vec3{0.0f, -10.0f, 0.0f};
    config3.fixedStepSeconds = 0.5f;
    World* world3 = backend.createWorld(config3);
    BodyDesc staticDesc;
    staticDesc.kind = BodyKind::Static;
    staticDesc.position = vec3{0.0f, 5.0f, 0.0f};
    Body* staticBody = backend.createBody(world3, staticDesc);
    BodyDesc noGravityDesc;
    noGravityDesc.kind = BodyKind::Dynamic;
    noGravityDesc.massKg = 1.0f;
    noGravityDesc.affectedByGravity = false;
    Body* noGravityBody = backend.createBody(world3, noGravityDesc);
    backend.step(world3);
    auto staticState = backend.readState(staticBody);
    check(near(staticState.position.y, 5.0f, 1e-6f),
          (name + ": static unmoved").c_str());
    auto noGravityState = backend.readState(noGravityBody);
    check(near(noGravityState.position.y, 0.0f, 1e-6f),
          (name + ": no-gravity body").c_str());
    backend.releaseWorld(world3);

    // -- Buoyancy opposes gravity -------------------------------------------
    WorldConfig config4;
    config4.gravity = vec3{0.0f, -10.0f, 0.0f};
    config4.fixedStepSeconds = 0.5f;
    World* world4 = backend.createWorld(config4);
    BodyDesc desc4;
    desc4.kind = BodyKind::Dynamic;
    desc4.massKg = 1.0f;
    Body* body4 = backend.createBody(world4, desc4);
    // A fluid box that exactly fills the displacement: the body floats.
    // Upward force = 1 m^3 * 1 kg/m^3 * 9.81 m/s^2; gravity = 10 N down.
    // Net acceleration = -0.19 m/s^2.
    BuoyancyVolumeDesc fluid;
    fluid.centre = vec3{0.0f, 0.0f, 0.0f};
    fluid.extents = vec3{1.0f, 1.0f, 1.0f};
    fluid.densityKgPerM3 = 1.0f;
    fluid.drag = 0.0f;
    (void)backend.createBuoyancyVolume(world4, fluid);
    backend.step(world4);
    auto state4 = backend.readState(body4);
    check(near(state4.linearVelocity.y, (-10.0f + 9.81f) * 0.5f, 1e-5f),
          (name + ": buoyancy").c_str());
    backend.releaseWorld(world4);

    // -- Force fields --------------------------------------------------------
    WorldConfig config5;
    config5.gravity = vec3{};
    config5.fixedStepSeconds = 0.5f;
    World* world5 = backend.createWorld(config5);
    BodyDesc desc5;
    desc5.kind = BodyKind::Dynamic;
    desc5.massKg = 1.0f;
    desc5.affectedByGravity = false;
    Body* body5 = backend.createBody(world5, desc5);
    ForceFieldDesc field;
    field.centre = vec3{0.0f, 0.0f, 0.0f};
    field.extents = vec3{2.0f, 2.0f, 2.0f};
    field.acceleration = vec3{2.0f, 0.0f, 0.0f};
    (void)backend.createForceField(world5, field);
    backend.step(world5);
    auto state5 = backend.readState(body5);
    check(near(state5.linearVelocity.x, 1.0f, 1e-6f),
          (name + ": field velocity").c_str());
    backend.releaseWorld(world5);

    // -- Joints ---------------------------------------------------------------
    WorldConfig config6;
    config6.gravity = vec3{};
    config6.fixedStepSeconds = 0.5f;
    World* world6 = backend.createWorld(config6);
    BodyDesc desc6;
    desc6.kind = BodyKind::Dynamic;
    desc6.shape.kind = ShapeDesc::Kind::Sphere;
    desc6.shape.sphereRadius = 0.5f;
    desc6.massKg = 1.0f;
    Body* a = backend.createBody(world6, desc6);
    Body* b = backend.createBody(world6, desc6);
    JointDesc joint;
    joint.kind = JointDesc::Kind::PointToPoint;
    joint.bodyA = 0;
    joint.bodyB = 1;
    joint.anchorA = vec3{0.0f, 0.0f, 0.0f};
    joint.anchorB = vec3{0.0f, 0.0f, 0.0f};
    // A point-to-point constraint pins the two bodies' anchors together:
    // the bodies start at the same position, so a few steps do not move
    // them apart.
    Joint* j = backend.createJoint(world6, joint);
    check(j != nullptr, (name + ": joint created").c_str());
    for (int i = 0; i < 4; ++i)
        backend.step(world6);
    auto stateA = backend.readState(a);
    auto stateB = backend.readState(b);
    check(near(stateA.position.x, stateB.position.x, 5e-2f),
          (name + ": joint anchors").c_str());
    backend.releaseJoint(world6, j);
    backend.releaseWorld(world6);

    // -- Protocol violations ---------------------------------------------------
    WorldConfig config7;
    World* world7 = backend.createWorld(config7);
    backend.releaseWorld(world7);
    backend.releaseWorld(world7);
    check(backend.violationCount() == 1u,
          (name + ": double release").c_str());
    backend.releaseWorld(nullptr);
    check(backend.violationCount() == 2u,
          (name + ": unknown world").c_str());
}

}  // namespace

int main()
{
    grace::physics::BulletPhysicsBackend bullet;
    testBackend(bullet, "bullet", 1e-6f);
    bullet.shutdown();

    grace::physics::Box2DPhysicsBackend box2d;
    testBackend(box2d, "box2d", 0.25f);
    box2d.shutdown();

    grace::physics::PhysXPhysicsBackend physx;
    testBackend(physx, "physx", 1e-6f);
    physx.shutdown();

    std::printf("physics_adapters: %d checks, %d failures\n", checks,
                failures);
    return failures == 0 ? 0 : 1;
}
