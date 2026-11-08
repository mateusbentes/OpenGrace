#include "grace/physics/box2d_physics_backend.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>

#include "box2d/box2d.h"

namespace grace::physics {

namespace {

/// Converts a contract position to a Box2D position (z is flat). The
/// contract's world is Y-up, which is Box2D's own convention.
b2Vec2 toBox2D(const core::vec3& v)
{
    b2Vec2 result;
    result.x = v.x;
    result.y = v.y;
    return result;
}

/// Converts a Box2D position back to a contract position (z=0).
core::vec3 fromBox2D(const b2Vec2& v)
{
    return core::vec3(v.x, v.y, 0.0f);
}

/// Converts a contract position to a Box2D body position (double precision
/// in large-world mode; z is flat).
b2Pos toBox2DPos(const core::vec3& v)
{
    b2Pos pos;
    pos.x = v.x;
    pos.y = v.y;
    return pos;
}

/// Converts a Box2D body position back to a contract position (z=0).
core::vec3 fromBox2DPos(const b2Pos& p)
{
    return core::vec3(static_cast<float>(p.x), static_cast<float>(p.y), 0.0f);
}

/// Converts a Box2D rotation (cos/sin) to an angle in radians.
float fromBox2DRot(const b2Rot& q)
{
    return std::atan2(q.s, q.c);
}

/// Converts an angle in radians to a Box2D rotation.
b2Rot toBox2DRot(float angle)
{
    b2Rot rot;
    rot.c = std::cos(angle);
    rot.s = std::sin(angle);
    return rot;
}

/// Concrete world: the Box2D world id plus the records the contract needs.
struct Box2DWorld : World
{
    b2WorldId worldId{};
    WorldConfig config;

    std::vector<std::unique_ptr<struct Box2DBody>> bodies;
    std::vector<std::unique_ptr<struct Box2DJoint>> joints;
    std::vector<std::unique_ptr<struct Box2DBuoyancy>> buoyancyVolumes;
    std::vector<std::unique_ptr<struct Box2DField>> forceFields;
    bool released = false;
};

/// Concrete body: the Box2D body id plus the contract's desc.
struct Box2DBody : Body
{
    b2BodyId bodyId{};
    b2BodyDef def;
    BodyDesc desc;
    bool released = false;
};

/// A joint: the Box2D joint id plus the contract's desc.
struct Box2DJoint : Joint
{
    b2JointId jointId{};
    JointDesc desc;
    bool released = false;
};

/// Buoyancy volume: the contract's desc, applied by the backend during the
/// step (Box2D has no buoyancy primitive).
struct Box2DBuoyancy : BuoyancyVolume
{
    BuoyancyVolumeDesc desc;
    bool released = false;
};

/// Force field: the contract's desc, applied by the backend during the step
/// (Box2D has no force field primitive).
struct Box2DField : ForceField
{
    ForceFieldDesc desc;
    bool released = false;
};

/// A body's world-space centre of mass. Axis-aligned overlap test on the
/// centre of mass, matching the null backend's behaviour.
bool pointInside(const core::vec3& p, const core::vec3& centre,
                 const core::vec3& extents)
{
    return std::fabs(p.x - centre.x) <= extents.x * 0.5f
        && std::fabs(p.y - centre.y) <= extents.y * 0.5f
        && std::fabs(p.z - centre.z) <= extents.z * 0.5f;
}

/// Attaches a contract shape to a Box2D body. The contract's capsule
/// becomes a Box2D capsule (Box2D 3 has one); the contract's z axis is
/// flattened to 0.
void attachShape(b2BodyId bodyId, const ShapeDesc& shape)
{
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    switch (shape.kind)
    {
    case ShapeDesc::Kind::Box:
    {
        const b2Polygon box = b2MakeBox(shape.boxHalfExtents.x,
                                        shape.boxHalfExtents.y);
        b2CreatePolygonShape(bodyId, &shapeDef, &box);
        return;
    }
    case ShapeDesc::Kind::Sphere:
    {
        b2Circle circle;
        circle.center = toBox2D(core::vec3(0.0f, 0.0f, 0.0f));
        circle.radius = shape.sphereRadius;
        b2CreateCircleShape(bodyId, &shapeDef, &circle);
        return;
    }
    case ShapeDesc::Kind::Capsule:
    {
        b2Capsule capsule;
        capsule.center1 = toBox2D(core::vec3(0.0f, -shape.capsuleHalfHeight, 0.0f));
        capsule.center2 = toBox2D(core::vec3(0.0f, shape.capsuleHalfHeight, 0.0f));
        capsule.radius = shape.capsuleRadius;
        b2CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        return;
    }
    }
}

/// Converts a core::vec3 to a Box2D joint frame.
b2Transform toBox2DFrame(const core::vec3& v)
{
    b2Transform frame;
    frame.p = toBox2D(v);
    frame.q = toBox2DRot(0.0f);
    return frame;
}

}  // namespace

struct Box2DPhysicsBackend::Impl
{
    std::vector<std::unique_ptr<Box2DWorld>> worlds;
    bool shutdown = false;
    std::uint64_t violations = 0;

    void countViolation() { ++violations; }

    bool ready() const { return !shutdown; }

    Box2DWorld* worldOf(World* world)
    {
        for (auto& w : worlds)
        {
            if (w.get() == world)
                return w.get();
        }
        return nullptr;
    }

    Box2DBody* bodyOf(Body* body)
    {
        for (auto& w : worlds)
        {
            for (auto& b : w->bodies)
            {
                if (static_cast<Body*>(b.get()) == body)
                    return b.get();
            }
        }
        return nullptr;
    }
};

Box2DPhysicsBackend::Box2DPhysicsBackend()
    : m_impl(std::make_unique<Impl>())
{
}

Box2DPhysicsBackend::~Box2DPhysicsBackend() = default;

std::string Box2DPhysicsBackend::describe() const
{
    return "box2d (Box2D 3, flat layer of the classical physics)";
}

std::uint32_t Box2DPhysicsBackend::capabilityFlags() const
{
    return kCapability2DOnly;
}

void Box2DPhysicsBackend::shutdown()
{
    if (m_impl->shutdown)
    {
        m_impl->countViolation();
        return;
    }
    m_impl->shutdown = true;
}

std::uint64_t Box2DPhysicsBackend::violationCount() const noexcept
{
    return m_impl->violations;
}

World* Box2DPhysicsBackend::createWorld(const WorldConfig& config)
{
    std::string message;
    if (!config.validate(message))
        throw std::invalid_argument(message);

    auto record = std::make_unique<Box2DWorld>();
    record->config = config;
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = toBox2D(config.gravity);
    record->worldId = b2CreateWorld(&worldDef);
    World* world = record.get();
    m_impl->worlds.push_back(std::move(record));
    return world;
}

void Box2DPhysicsBackend::releaseWorld(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        return;
    }
    for (auto& b : record->bodies)
        b->released = true;
    for (auto& j : record->joints)
        j->released = true;
    for (auto& v : record->buoyancyVolumes)
        v->released = true;
    for (auto& f : record->forceFields)
        f->released = true;
    record->released = true;
}

Body* Box2DPhysicsBackend::createBody(World* world, const BodyDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    Box2DWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto body = std::make_unique<Box2DBody>();
    body->desc = desc;
    body->def = b2DefaultBodyDef();
    body->def.position = toBox2DPos(desc.position);
    body->def.type = desc.kind == BodyKind::Dynamic ? b2_dynamicBody
                                                    : b2_staticBody;
    body->def.linearDamping = desc.linearDamping;
    body->def.angularDamping = desc.angularDamping;
    body->def.gravityScale = desc.affectedByGravity ? 1.0f : 0.0f;
    body->bodyId = b2CreateBody(record->worldId, &body->def);
    attachShape(body->bodyId, desc.shape);

    // The contract carries an explicit mass; the shape density is a fixed
    // nominal value, so the mass is set through b2Body_SetMassData. The
    // inertia is scaled from the shape's nominal value so that dynamic
    // bodies keep a stable rotation.
    if (desc.kind == BodyKind::Dynamic)
    {
        b2MassData massData = b2Body_GetMassData(body->bodyId);
        massData.mass = desc.massKg;
        b2Body_SetMassData(body->bodyId, massData);
    }
    Body* handle = body.get();
    record->bodies.push_back(std::move(body));
    return handle;
}

void Box2DPhysicsBackend::releaseBody(World* world, Body* body)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DWorld* record = m_impl->worldOf(world);
    if (!record)
    {
        m_impl->countViolation();
        return;
    }
    Box2DBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->released = true;
    b2DestroyBody(b->bodyId);
}

void Box2DPhysicsBackend::applyForce(Body* body, const core::vec3& force)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b2Body_ApplyForceToCenter(b->bodyId, toBox2D(force), true);
}

void Box2DPhysicsBackend::applyImpulse(Body* body, const core::vec3& impulse)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b2Body_ApplyLinearImpulseToCenter(b->bodyId, toBox2D(impulse), true);
}

void Box2DPhysicsBackend::step(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        return;
    }

    // Buoyancy and force fields are applied per step, before the Box2D
    // integration (Box2D has no such primitives).
    for (auto& b : record->bodies)
    {
        if (b->released || b->desc.kind != BodyKind::Dynamic)
            continue;
        const core::vec3 position = fromBox2DPos(b2Body_GetPosition(b->bodyId));
        for (const auto& v : record->buoyancyVolumes)
        {
            if (v->released)
                continue;
            if (!pointInside(position, v->desc.centre, v->desc.extents))
                continue;
            // The contract's buoyancy measures the volume the body's centre
            // of mass displaces, the same measure every backend applies
            // (the null reference uses the full 3D volume).
            const float volume = v->desc.extents.x * v->desc.extents.y
                               * v->desc.extents.z;
            const float displacedMass =
                v->desc.densityKgPerM3 * volume * b->desc.massKg;
            b2Body_ApplyForceToCenter(
                b->bodyId, toBox2D(core::vec3(0.0f, displacedMass * 9.81f, 0.0f)),
                true);
            // The contract's drag is a per-second acceleration proportional
            // to the velocity, so it is applied as mass * (-v * drag).
            // The contract's drag is a per-second acceleration proportional
            // to the velocity, so it is applied as mass * (-v * drag).
            const b2Vec2 velocity = b2Body_GetLinearVelocity(b->bodyId);
            b2Vec2 drag;
            drag.x = velocity.x * -v->desc.drag * b->desc.massKg;
            drag.y = velocity.y * -v->desc.drag * b->desc.massKg;
            b2Body_ApplyForceToCenter(b->bodyId, drag, true);
        }
        for (const auto& f : record->forceFields)
        {
            if (f->released)
                continue;
            if (pointInside(position, f->desc.centre, f->desc.extents))
                b2Body_ApplyForceToCenter(
                    b->bodyId,
                    toBox2D(core::vec3(f->desc.acceleration.x,
                                       f->desc.acceleration.y, 0.0f)),
                    true);
        }
    }

    b2World_Step(record->worldId, record->config.fixedStepSeconds, 8);
}

RigidBodyState Box2DPhysicsBackend::readState(Body* body) const
{
    for (const auto& w : m_impl->worlds)
    {
        for (const auto& b : w->bodies)
        {
            if (static_cast<Body*>(b.get()) == body)
            {
                RigidBodyState state;
                state.position =
                    fromBox2DPos(b2Body_GetPosition(b->bodyId));
                state.rotation =
                    core::vec3(0.0f, 0.0f,
                               fromBox2DRot(b2Body_GetRotation(b->bodyId)));
                state.linearVelocity =
                    fromBox2D(b2Body_GetLinearVelocity(b->bodyId));
                state.angularVelocity =
                    core::vec3(0.0f, 0.0f,
                               b2Body_GetAngularVelocity(b->bodyId));
                return state;
            }
        }
    }
    return RigidBodyState{};
}

Joint* Box2DPhysicsBackend::createJoint(World* world, const JointDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    Box2DWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    if (desc.bodyA >= record->bodies.size()
        || desc.bodyB >= record->bodies.size())
    {
        throw std::invalid_argument("joint body index out of range");
    }

    auto joint = std::make_unique<Box2DJoint>();
    joint->desc = desc;
    if (desc.kind == JointDesc::Kind::Distance)
    {
        b2DistanceJointDef def = b2DefaultDistanceJointDef();
        def.base.bodyIdA = record->bodies[desc.bodyA]->bodyId;
        def.base.bodyIdB = record->bodies[desc.bodyB]->bodyId;
        def.base.localFrameA = toBox2DFrame(desc.anchorA);
        def.base.localFrameB = toBox2DFrame(desc.anchorB);
        def.length = desc.restLength;
        joint->jointId = b2CreateDistanceJoint(record->worldId, &def);
    }
    else
    {
        // Point-to-point constraint: a Box2D 3 revolute joint with no
        // motor and no limit.
        b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
        def.base.bodyIdA = record->bodies[desc.bodyA]->bodyId;
        def.base.bodyIdB = record->bodies[desc.bodyB]->bodyId;
        def.base.localFrameA = toBox2DFrame(desc.anchorA);
        def.base.localFrameB = toBox2DFrame(desc.anchorB);
        def.enableLimit = false;
        def.enableMotor = false;
        joint->jointId = b2CreateRevoluteJoint(record->worldId, &def);
    }
    Joint* handle = joint.get();
    record->joints.push_back(std::move(joint));
    return handle;
}

void Box2DPhysicsBackend::releaseJoint(World* world, Joint* joint)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DWorld* record = m_impl->worldOf(world);
    if (!record)
    {
        m_impl->countViolation();
        return;
    }
    for (auto& j : record->joints)
    {
        if (j.get() == joint)
        {
            if (j->released)
            {
                m_impl->countViolation();
                return;
            }
            j->released = true;
            b2DestroyJoint(j->jointId);
            return;
        }
    }
    m_impl->countViolation();
}

BuoyancyVolume* Box2DPhysicsBackend::createBuoyancyVolume(
    World* world, const BuoyancyVolumeDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    Box2DWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto volume = std::make_unique<Box2DBuoyancy>();
    volume->desc = desc;
    BuoyancyVolume* handle = volume.get();
    record->buoyancyVolumes.push_back(std::move(volume));
    return handle;
}

void Box2DPhysicsBackend::releaseBuoyancyVolume(World* world,
                                                BuoyancyVolume* volume)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DWorld* record = m_impl->worldOf(world);
    if (!record)
    {
        m_impl->countViolation();
        return;
    }
    for (auto& v : record->buoyancyVolumes)
    {
        if (v.get() == volume)
        {
            if (v->released)
            {
                m_impl->countViolation();
                return;
            }
            v->released = true;
            return;
        }
    }
    m_impl->countViolation();
}

ForceField* Box2DPhysicsBackend::createForceField(World* world,
                                                  const ForceFieldDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    Box2DWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto field = std::make_unique<Box2DField>();
    field->desc = desc;
    ForceField* handle = field.get();
    record->forceFields.push_back(std::move(field));
    return handle;
}

void Box2DPhysicsBackend::releaseForceField(World* world, ForceField* field)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    Box2DWorld* record = m_impl->worldOf(world);
    if (!record)
    {
        m_impl->countViolation();
        return;
    }
    for (auto& f : record->forceFields)
    {
        if (f.get() == field)
        {
            if (f->released)
            {
                m_impl->countViolation();
                return;
            }
            f->released = true;
            return;
        }
    }
    m_impl->countViolation();
}

}  // namespace grace::physics
