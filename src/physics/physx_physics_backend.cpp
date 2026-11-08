#include "grace/physics/physx_physics_backend.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>

// The PhysX 3.4 headers require exactly one of NDEBUG / _DEBUG to be defined.
// This module is compiled in the project's build types, both of which define
// NDEBUG (Debug builds of this project are NDEBUG-clean, the engine's rule);
// with CMake, the Release and Debug flags are handled below in CMakeLists.
#if !defined(NDEBUG) && !defined(_DEBUG)
#define NDEBUG
#endif

#include "PxPhysicsAPI.h"

namespace grace::physics {

namespace {

/// The default allocator/error callbacks are objects, not singletons: the
/// backend owns one of each for the foundation's lifetime.
physx::PxDefaultAllocator gAllocator;
physx::PxDefaultErrorCallback gErrorCallback;

/// Converts a core::vec3 to a PhysX vector.
physx::PxVec3 toPhysX(const core::vec3& v)
{
    return physx::PxVec3(v.x, v.y, v.z);
}

/// Converts a PhysX vector to a core::vec3.
core::vec3 fromPhysX(const physx::PxVec3& v)
{
    return core::vec3(v.x, v.y, v.z);
}

/// Converts a PhysX quaternion to Euler angles in radians, X, then Y, then
/// Z order (the engine's Transform convention).
core::vec3 toEuler(const physx::PxQuat& q)
{
    const float sqw = q.w * q.w;
    const float sqx = q.x * q.x;
    const float sqy = q.y * q.y;
    const float sqz = q.z * q.z;

    const float m21 = 2.0f * (q.y * q.z + q.x * q.w);
    const float m11 = sqw - sqx - sqy + sqz;
    const float m32 = 2.0f * (q.z * q.x + q.y * q.w);
    const float m22 = sqw - sqx + sqy - sqz;
    const float m13 = 2.0f * (q.x * q.y + q.z * q.w);
    const float m33 = sqw - sqx - sqy - sqz;

    return core::vec3(std::atan2(m21, m11), std::atan2(m32, m22),
                      std::atan2(m13, m33));
}

/// Concrete world: the PhysX scene plus the records the contract needs.
struct PhysXWorld : World
{
    physx::PxScene* scene = nullptr;
    WorldConfig config;

    std::vector<std::unique_ptr<struct PhysXBody>> bodies;
    std::vector<std::unique_ptr<struct PhysXJoint>> joints;
    std::vector<std::unique_ptr<struct PhysXBuoyancy>> buoyancyVolumes;
    std::vector<std::unique_ptr<struct PhysXField>> forceFields;
    bool released = false;
};

/// Concrete body: the PhysX actor plus the contract's desc.
struct PhysXBody : Body
{
    physx::PxRigidDynamic* dynamic = nullptr;
    physx::PxRigidStatic* staticActor = nullptr;
    BodyDesc desc;
    bool released = false;
};

/// A joint: the PhysX constraint plus the contract's desc.
struct PhysXJoint : Joint
{
    physx::PxDistanceJoint* joint = nullptr;
    JointDesc desc;
    bool released = false;
};

/// Buoyancy volume: the contract's desc, applied by the backend during the
/// step (PhysX has no buoyancy primitive).
struct PhysXBuoyancy : BuoyancyVolume
{
    BuoyancyVolumeDesc desc;
    bool released = false;
};

/// Force field: the contract's desc, applied by the backend during the step
/// (PhysX has no force field primitive).
struct PhysXField : ForceField
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

/// Returns the actor of a body record.
physx::PxRigidActor* actorOf(PhysXBody* body)
{
    return body->dynamic ? static_cast<physx::PxRigidActor*>(body->dynamic)
                         : static_cast<physx::PxRigidActor*>(body->staticActor);
}

/// Creates the PhysX geometry of a contract shape.
physx::PxGeometryHolder makeGeometry(const ShapeDesc& shape)
{
    switch (shape.kind)
    {
    case ShapeDesc::Kind::Box:
        return physx::PxBoxGeometry(
            toPhysX(core::vec3(shape.boxHalfExtents.x, shape.boxHalfExtents.y,
                               shape.boxHalfExtents.z)));
    case ShapeDesc::Kind::Sphere:
        return physx::PxSphereGeometry(shape.sphereRadius);
    case ShapeDesc::Kind::Capsule:
        // PhysX capsules grow along their local X axis; the contract's
        // capsule grows along Y, so the shape is rotated at the transform.
        return physx::PxCapsuleGeometry(shape.capsuleRadius,
                                        shape.capsuleHalfHeight);
    }
    return physx::PxBoxGeometry(physx::PxVec3(0.5f, 0.5f, 0.5f));
}

}  // namespace

struct PhysXPhysicsBackend::Impl
{
    physx::PxFoundation* foundation = nullptr;
    physx::PxPhysics* physics = nullptr;
    physx::PxCpuDispatcher* dispatcher = nullptr;
    std::vector<std::unique_ptr<PhysXWorld>> worlds;
    bool shutdown = false;
    std::uint64_t violations = 0;

    void countViolation() { ++violations; }

    bool ready() const { return !shutdown; }

    PhysXWorld* worldOf(World* world)
    {
        for (auto& w : worlds)
        {
            if (w.get() == world)
                return w.get();
        }
        return nullptr;
    }

    PhysXBody* bodyOf(Body* body)
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

PhysXPhysicsBackend::PhysXPhysicsBackend()
    : m_impl(std::make_unique<Impl>())
{
    // The foundation is a separate versioned module from the physics SDK
    // (PhysX 3.4's foundation is version 1.0, not the physics version).
    m_impl->foundation = PxCreateFoundation(PX_FOUNDATION_VERSION,
                                            gAllocator, gErrorCallback);
    m_impl->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_impl->foundation,
                                      physx::PxTolerancesScale());
}

PhysXPhysicsBackend::~PhysXPhysicsBackend()
{
    for (auto& w : m_impl->worlds)
    {
        if (w->scene)
            w->scene->release();
    }
    if (m_impl->dispatcher)
    {
        static_cast<physx::PxDefaultCpuDispatcher*>(m_impl->dispatcher)
            ->release();
    }
    if (m_impl->physics)
        m_impl->physics->release();
    if (m_impl->foundation)
        m_impl->foundation->release();
}

std::string PhysXPhysicsBackend::describe() const
{
    return "physx (PhysX 3.4, fixed step from the world config)";
}

std::uint32_t PhysXPhysicsBackend::capabilityFlags() const
{
    // The extra physics: CPU cloth and soft body are the PhysX 3.4
    // capability the original GRACE 2 engine called out (decision 32).
    return kCapabilityCloth | kCapabilitySoftBody;
}

void PhysXPhysicsBackend::shutdown()
{
    if (m_impl->shutdown)
    {
        m_impl->countViolation();
        return;
    }
    m_impl->shutdown = true;
}

std::uint64_t PhysXPhysicsBackend::violationCount() const noexcept
{
    return m_impl->violations;
}

World* PhysXPhysicsBackend::createWorld(const WorldConfig& config)
{
    std::string message;
    if (!config.validate(message))
        throw std::invalid_argument(message);

    physx::PxSceneDesc sceneDesc(m_impl->physics->getTolerancesScale());
    sceneDesc.gravity = toPhysX(config.gravity);
    if (!m_impl->dispatcher)
    {
        m_impl->dispatcher = physx::PxDefaultCpuDispatcherCreate(1);
    }
    sceneDesc.cpuDispatcher = m_impl->dispatcher;
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

    auto record = std::make_unique<PhysXWorld>();
    record->config = config;
    record->scene = m_impl->physics->createScene(sceneDesc);
    World* world = record.get();
    m_impl->worlds.push_back(std::move(record));
    return world;
}

void PhysXPhysicsBackend::releaseWorld(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXWorld* record = m_impl->worldOf(world);
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

Body* PhysXPhysicsBackend::createBody(World* world, const BodyDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    PhysXWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto body = std::make_unique<PhysXBody>();
    body->desc = desc;

    physx::PxMaterial* material =
        m_impl->physics->createMaterial(0.5f, 0.5f, 0.1f);
    physx::PxTransform transform(toPhysX(desc.position));
    physx::PxGeometry& geometry = makeGeometry(desc.shape).any();

    if (desc.kind == BodyKind::Dynamic)
    {
        body->dynamic = m_impl->physics->createRigidDynamic(transform);
        body->dynamic->createShape(geometry, *material);
        // The contract carries an explicit mass; setMassAndUpdateInertia
        // sets it and derives the inertia from the shape's geometry.
        physx::PxRigidBodyExt::setMassAndUpdateInertia(*body->dynamic,
                                                       desc.massKg);
        body->dynamic->setLinearDamping(desc.linearDamping);
        body->dynamic->setAngularDamping(desc.angularDamping);
        if (!desc.affectedByGravity)
        {
            body->dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY,
                                        true);
        }
        record->scene->addActor(*body->dynamic);
    }
    else
    {
        body->staticActor = m_impl->physics->createRigidStatic(transform);
        body->staticActor->createShape(geometry, *material);
        record->scene->addActor(*body->staticActor);
    }
    Body* handle = body.get();
    record->bodies.push_back(std::move(body));
    return handle;
}

void PhysXPhysicsBackend::releaseBody(World* world, Body* body)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXWorld* record = m_impl->worldOf(world);
    if (!record)
    {
        m_impl->countViolation();
        return;
    }
    PhysXBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->released = true;
    record->scene->removeActor(*actorOf(b));
    actorOf(b)->release();
}

void PhysXPhysicsBackend::applyForce(Body* body, const core::vec3& force)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    if (b->dynamic)
        b->dynamic->addForce(toPhysX(force), physx::PxForceMode::eFORCE);
}

void PhysXPhysicsBackend::applyImpulse(Body* body, const core::vec3& impulse)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    if (b->dynamic)
        b->dynamic->addForce(toPhysX(impulse), physx::PxForceMode::eIMPULSE);
}

void PhysXPhysicsBackend::step(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        return;
    }

    // Buoyancy and force fields are applied per step, before the PhysX
    // integration (PhysX has no such primitives). The mass displaced uses
    // the volume the body's centre of mass occupies, the same measure the
    // null backend applies (architecture section 3.6).
    for (auto& b : record->bodies)
    {
        if (b->released || b->desc.kind != BodyKind::Dynamic || !b->dynamic)
            continue;
        const core::vec3 position = fromPhysX(b->dynamic->getGlobalPose().p);
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
            b->dynamic->addForce(
                physx::PxVec3(0.0f, displacedMass * 9.81f, 0.0f),
                physx::PxForceMode::eFORCE);
            // The contract's drag is a per-second acceleration proportional
            // to the velocity, so it is applied as mass * (-v * drag).
            const physx::PxVec3 drag = b->dynamic->getLinearVelocity()
                * -v->desc.drag * b->desc.massKg;
            b->dynamic->addForce(drag, physx::PxForceMode::eFORCE);
        }
        for (const auto& f : record->forceFields)
        {
            if (f->released)
                continue;
            if (pointInside(position, f->desc.centre, f->desc.extents))
            {
                b->dynamic->addForce(
                    toPhysX(f->desc.acceleration) * b->desc.massKg,
                    physx::PxForceMode::eFORCE);
            }
        }
    }

    record->scene->simulate(record->config.fixedStepSeconds);
    record->scene->fetchResults(true);
}

RigidBodyState PhysXPhysicsBackend::readState(Body* body) const
{
    for (const auto& w : m_impl->worlds)
    {
        for (const auto& b : w->bodies)
        {
            if (static_cast<Body*>(b.get()) == body)
            {
                RigidBodyState state;
                if (b->dynamic)
                {
                    state.position =
                        fromPhysX(b->dynamic->getGlobalPose().p);
                    state.rotation =
                        toEuler(b->dynamic->getGlobalPose().q);
                    state.linearVelocity =
                        fromPhysX(b->dynamic->getLinearVelocity());
                    state.angularVelocity =
                        fromPhysX(b->dynamic->getAngularVelocity());
                }
                else if (b->staticActor)
                {
                    state.position =
                        fromPhysX(b->staticActor->getGlobalPose().p);
                    state.rotation =
                        toEuler(b->staticActor->getGlobalPose().q);
                }
                return state;
            }
        }
    }
    return RigidBodyState{};
}

Joint* PhysXPhysicsBackend::createJoint(World* world, const JointDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    PhysXWorld* record = m_impl->worldOf(world);
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

    auto joint = std::make_unique<PhysXJoint>();
    joint->desc = desc;
    PhysXBody* a = record->bodies[desc.bodyA].get();
    PhysXBody* b = record->bodies[desc.bodyB].get();

    // Both joint kinds are a PhysX distance joint: a distance joint with
    // the rest length is the contract's distance joint; with the rest
    // length 0 it pins the two anchors together (the point-to-point
    // constraint). PhysX 3.4 has no plain point-to-point primitive.
    physx::PxDistanceJoint* distance = physx::PxDistanceJointCreate(
        *m_impl->physics, actorOf(a),
        physx::PxTransform(toPhysX(desc.anchorA)),
        actorOf(b),
        physx::PxTransform(toPhysX(desc.anchorB)));
    distance->setMinDistance(
        desc.kind == JointDesc::Kind::Distance ? desc.restLength : 0.0f);
    distance->setMaxDistance(
        desc.kind == JointDesc::Kind::Distance ? desc.restLength : 0.0f);
    distance->setDistanceJointFlags(
        physx::PxDistanceJointFlag::eMIN_DISTANCE_ENABLED
        | physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED);
    distance->setConstraintFlag(
        physx::PxConstraintFlag::eCOLLISION_ENABLED, false);
    joint->joint = distance;
    Joint* handle = joint.get();
    record->joints.push_back(std::move(joint));
    return handle;
}

void PhysXPhysicsBackend::releaseJoint(World* world, Joint* joint)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXWorld* record = m_impl->worldOf(world);
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
            if (j->joint)
                j->joint->release();
            return;
        }
    }
    m_impl->countViolation();
}

BuoyancyVolume* PhysXPhysicsBackend::createBuoyancyVolume(
    World* world, const BuoyancyVolumeDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    PhysXWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto volume = std::make_unique<PhysXBuoyancy>();
    volume->desc = desc;
    BuoyancyVolume* handle = volume.get();
    record->buoyancyVolumes.push_back(std::move(volume));
    return handle;
}

void PhysXPhysicsBackend::releaseBuoyancyVolume(World* world,
                                                BuoyancyVolume* volume)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXWorld* record = m_impl->worldOf(world);
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

ForceField* PhysXPhysicsBackend::createForceField(World* world,
                                                  const ForceFieldDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    PhysXWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto field = std::make_unique<PhysXField>();
    field->desc = desc;
    ForceField* handle = field.get();
    record->forceFields.push_back(std::move(field));
    return handle;
}

void PhysXPhysicsBackend::releaseForceField(World* world, ForceField* field)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    PhysXWorld* record = m_impl->worldOf(world);
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
