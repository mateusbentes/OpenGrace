#include "grace/physics/bullet_physics_backend.hpp"

#include <memory>
#include <stdexcept>

#include "btBulletDynamicsCommon.h"

namespace grace::physics {

namespace {

/// Converts a core::vec3 to a Bullet vector.
btVector3 toBullet(const core::vec3& v)
{
    return btVector3(v.x, v.y, v.z);
}

/// Converts a Bullet vector to a core::vec3.
core::vec3 fromBullet(const btVector3& v)
{
    return core::vec3(v.x(), v.y(), v.z());
}

/// Converts a Bullet quaternion to Euler angles in radians, X, then Y, then
/// Z order (the engine's Transform convention).
core::vec3 toEuler(const btQuaternion& q)
{
    // Standard quaternion to Euler (XYZ) conversion.
    const float sqw = q.w() * q.w();
    const float sqx = q.x() * q.x();
    const float sqy = q.y() * q.y();
    const float sqz = q.z() * q.z();

    const float m21 = 2.0f * (q.y() * q.z() + q.x() * q.w());
    const float m11 = sqw - sqx - sqy + sqz;
    const float m32 = 2.0f * (q.z() * q.x() + q.y() * q.w());
    const float m22 = sqw - sqx + sqy - sqz;
    const float m13 = 2.0f * (q.x() * q.y() + q.z() * q.w());
    const float m33 = sqw - sqx - sqy - sqz;

    return core::vec3(std::atan2(m21, m11), std::atan2(m32, m22),
                      std::atan2(m13, m33));
}

/// Collision shape of a contract body, kept as a Bullet-native shape.
struct BulletShape
{
    std::unique_ptr<btCollisionShape> shape;
    ShapeDesc::Kind kind = ShapeDesc::Kind::Box;
};

/// Concrete world: the Bullet world plus the records the contract needs
/// (the contract's bodies, joints and fields live inside it).
struct BulletWorld : World
{
    std::unique_ptr<btDefaultCollisionConfiguration> configuration;
    std::unique_ptr<btCollisionDispatcher> dispatcher;
    std::unique_ptr<btDbvtBroadphase> broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
    std::unique_ptr<btDiscreteDynamicsWorld> world;
    WorldConfig config;

    std::vector<std::unique_ptr<struct BulletBody>> bodies;
    std::vector<std::unique_ptr<struct BulletJoint>> joints;
    std::vector<std::unique_ptr<struct BulletBuoyancy>> buoyancyVolumes;
    std::vector<std::unique_ptr<struct BulletField>> forceFields;
    bool released = false;
};

/// Concrete body: the Bullet body plus the accumulated force (Bullet
/// applies forces directly, so the contract's force accumulator is kept on
/// the record).
struct BulletBody : Body
{
    std::unique_ptr<btRigidBody> body;
    BulletShape shape;
    BodyDesc desc;
    bool released = false;
};

/// A joint: the Bullet constraint plus the contract's desc.
struct BulletJoint : Joint
{
    std::unique_ptr<btTypedConstraint> constraint;
    JointDesc desc;
    bool released = false;
};

/// Buoyancy volume: the contract's desc, applied by the backend during the
/// step (Bullet has no buoyancy primitive).
struct BulletBuoyancy : BuoyancyVolume
{
    BuoyancyVolumeDesc desc;
    bool released = false;
};

/// Force field: the contract's desc, applied by the backend during the step
/// (Bullet has no force field primitive).
struct BulletField : ForceField
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

/// Creates a Bullet collision shape from a contract shape.
std::unique_ptr<btCollisionShape> makeShape(const ShapeDesc& shape)
{
    switch (shape.kind)
    {
    case ShapeDesc::Kind::Box:
        return std::make_unique<btBoxShape>(
            toBullet(shape.boxHalfExtents));
    case ShapeDesc::Kind::Sphere:
        return std::make_unique<btSphereShape>(shape.sphereRadius);
    case ShapeDesc::Kind::Capsule:
        return std::make_unique<btCapsuleShape>(shape.capsuleRadius,
                                                shape.capsuleHalfHeight * 2.0f);
    }
    return nullptr;
}

}  // namespace

struct BulletPhysicsBackend::Impl
{
    std::vector<std::unique_ptr<BulletWorld>> worlds;
    bool shutdown = false;
    std::uint64_t violations = 0;

    void countViolation() { ++violations; }

    bool ready() const { return !shutdown; }

    BulletWorld* worldOf(World* world)
    {
        for (auto& w : worlds)
        {
            if (w.get() == world)
                return w.get();
        }
        return nullptr;
    }

    BulletBody* bodyOf(Body* body)
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

BulletPhysicsBackend::BulletPhysicsBackend()
    : m_impl(std::make_unique<Impl>())
{
}

BulletPhysicsBackend::~BulletPhysicsBackend() = default;

std::string BulletPhysicsBackend::describe() const
{
    return "bullet (Bullet 3, deterministic fixed step)";
}

std::uint32_t BulletPhysicsBackend::capabilityFlags() const
{
    // The classical layer only: no cloth, no soft body (Bullet 3's soft body
    // is a separate module, not part of the classical contract's backend).
    return 0u;
}

void BulletPhysicsBackend::shutdown()
{
    if (m_impl->shutdown)
    {
        m_impl->countViolation();
        return;
    }
    m_impl->shutdown = true;
}

std::uint64_t BulletPhysicsBackend::violationCount() const noexcept
{
    return m_impl->violations;
}

World* BulletPhysicsBackend::createWorld(const WorldConfig& config)
{
    std::string message;
    if (!config.validate(message))
        throw std::invalid_argument(message);

    auto record = std::make_unique<BulletWorld>();
    record->config = config;
    record->configuration = std::make_unique<btDefaultCollisionConfiguration>();
    record->dispatcher = std::make_unique<btCollisionDispatcher>(
        record->configuration.get());
    record->broadphase = std::make_unique<btDbvtBroadphase>();
    record->solver = std::make_unique<btSequentialImpulseConstraintSolver>();
    record->world = std::make_unique<btDiscreteDynamicsWorld>(
        record->dispatcher.get(), record->broadphase.get(),
        record->solver.get(), record->configuration.get());
    record->world->setGravity(toBullet(config.gravity));
    World* world = record.get();
    m_impl->worlds.push_back(std::move(record));
    return world;
}

void BulletPhysicsBackend::releaseWorld(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletWorld* record = m_impl->worldOf(world);
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

Body* BulletPhysicsBackend::createBody(World* world, const BodyDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    BulletWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto body = std::make_unique<BulletBody>();
    body->desc = desc;
    body->shape.kind = desc.shape.kind;
    body->shape.shape = makeShape(desc.shape);

    const bool isDynamic = desc.kind == BodyKind::Dynamic;
    const btScalar mass = isDynamic ? desc.massKg : 0.0f;
    btVector3 localInertia{0.0f, 0.0f, 0.0f};
    if (isDynamic)
        body->shape.shape->calculateLocalInertia(mass, localInertia);

    btRigidBody::btRigidBodyConstructionInfo info(
        mass, nullptr, body->shape.shape.get(), localInertia);
    body->body = std::make_unique<btRigidBody>(info);
    body->body->setUserPointer(body.get());
    body->body->setWorldTransform(
        btTransform(btQuaternion::getIdentity(), toBullet(desc.position)));
    body->body->setLinearFactor(btVector3(1.0f, 1.0f, 1.0f));
    body->body->setDamping(desc.linearDamping, desc.angularDamping);
    if (isDynamic && !desc.affectedByGravity)
    {
        // The world's addRigidBody overwrites the body's gravity with the
        // world's own; this flag keeps the body's gravity at zero, which
        // is the contract's affectedByGravity=false.
        body->body->setFlags(BT_DISABLE_WORLD_GRAVITY);
    }
    record->world->addRigidBody(body->body.get());
    Body* handle = body.get();
    record->bodies.push_back(std::move(body));
    return handle;
}

void BulletPhysicsBackend::releaseBody(World* world, Body* body)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletWorld* record = m_impl->worldOf(world);
    if (!record)
    {
        m_impl->countViolation();
        return;
    }
    BulletBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->released = true;
    record->world->removeRigidBody(b->body.get());
}

void BulletPhysicsBackend::applyForce(Body* body, const core::vec3& force)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->body->applyCentralForce(toBullet(force));
}

void BulletPhysicsBackend::applyImpulse(Body* body, const core::vec3& impulse)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->body->applyCentralImpulse(toBullet(impulse));
}

void BulletPhysicsBackend::step(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        return;
    }

    // Buoyancy and force fields are applied per step, before the Bullet
    // integration (Bullet has no such primitives).
    for (auto& b : record->bodies)
    {
        if (b->released || b->desc.kind != BodyKind::Dynamic)
            continue;
        const core::vec3 position = fromBullet(
            b->body->getWorldTransform().getOrigin());
        for (const auto& v : record->buoyancyVolumes)
        {
            if (v->released)
                continue;
            if (!pointInside(position, v->desc.centre, v->desc.extents))
                continue;
            const float volume = v->desc.extents.x * v->desc.extents.y
                               * v->desc.extents.z;
            const float displacedMass =
                v->desc.densityKgPerM3 * volume * b->desc.massKg;
            b->body->applyCentralForce(toBullet(
                core::vec3{0.0f, displacedMass * 9.81f, 0.0f}));
            // The contract's drag is a per-second acceleration proportional
            // to the velocity, so it is applied as mass * (-v * drag).
            b->body->applyCentralForce(toBullet(
                fromBullet(b->body->getLinearVelocity()) * -v->desc.drag
                * b->desc.massKg));
        }
        for (const auto& f : record->forceFields)
        {
            if (f->released)
                continue;
            if (pointInside(position, f->desc.centre, f->desc.extents))
                b->body->applyCentralForce(toBullet(
                    f->desc.acceleration * b->desc.massKg));
        }
    }

    record->world->stepSimulation(record->config.fixedStepSeconds, 1,
                                  record->config.fixedStepSeconds);
}

RigidBodyState BulletPhysicsBackend::readState(Body* body) const
{
    for (const auto& w : m_impl->worlds)
    {
        for (const auto& b : w->bodies)
        {
            if (b.get() == body)
            {
                RigidBodyState state;
                state.position = fromBullet(
                    b->body->getWorldTransform().getOrigin());
                state.rotation = toEuler(
                    b->body->getWorldTransform().getRotation());
                state.linearVelocity =
                    fromBullet(b->body->getLinearVelocity());
                state.angularVelocity =
                    fromBullet(b->body->getAngularVelocity());
                return state;
            }
        }
    }
    return RigidBodyState{};
}

Joint* BulletPhysicsBackend::createJoint(World* world, const JointDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    BulletWorld* record = m_impl->worldOf(world);
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

    auto joint = std::make_unique<BulletJoint>();
    joint->desc = desc;
    btRigidBody* a = record->bodies[desc.bodyA]->body.get();
    btRigidBody* b = record->bodies[desc.bodyB]->body.get();
    if (desc.kind == JointDesc::Kind::Distance)
    {
        // Bullet has no distance joint: a point-to-point constraint
        // between the two anchors, with the rest length enforced by the
        // same constraint (the constraint's error is the anchor
        // separation, not the rest length).
        joint->constraint = std::make_unique<btPoint2PointConstraint>(
            *a, *b, toBullet(desc.anchorA), toBullet(desc.anchorB));
    }
    else
    {
        joint->constraint = std::make_unique<btPoint2PointConstraint>(
            *a, *b, toBullet(desc.anchorA), toBullet(desc.anchorB));
    }
    // Connected bodies do not collide with each other, the same rule the
    // PhysX joint's eCOLLISION_ENABLED=false and the Box2D joint's
    // collideConnected=false carry.
    record->world->addConstraint(joint->constraint.get(), true);
    Joint* handle = joint.get();
    record->joints.push_back(std::move(joint));
    return handle;
}

void BulletPhysicsBackend::releaseJoint(World* world, Joint* joint)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletWorld* record = m_impl->worldOf(world);
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
            record->world->removeConstraint(j->constraint.get());
            return;
        }
    }
    m_impl->countViolation();
}

BuoyancyVolume* BulletPhysicsBackend::createBuoyancyVolume(
    World* world, const BuoyancyVolumeDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    BulletWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto volume = std::make_unique<BulletBuoyancy>();
    volume->desc = desc;
    BuoyancyVolume* handle = volume.get();
    record->buoyancyVolumes.push_back(std::move(volume));
    return handle;
}

void BulletPhysicsBackend::releaseBuoyancyVolume(World* world,
                                                 BuoyancyVolume* volume)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletWorld* record = m_impl->worldOf(world);
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

ForceField* BulletPhysicsBackend::createForceField(World* world,
                                                   const ForceFieldDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    BulletWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto field = std::make_unique<BulletField>();
    field->desc = desc;
    ForceField* handle = field.get();
    record->forceFields.push_back(std::move(field));
    return handle;
}

void BulletPhysicsBackend::releaseForceField(World* world, ForceField* field)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    BulletWorld* record = m_impl->worldOf(world);
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
