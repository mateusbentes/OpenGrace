#include "grace/physics/null_physics_backend.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>

namespace grace::physics {

namespace {

/// Concrete world handle. The null backend owns every body record, joint
/// desc, buoyancy volume and force field inside it.
struct NullWorld : World
{
    WorldConfig config;
    std::vector<std::unique_ptr<struct NullBody>> bodies;
    std::vector<std::unique_ptr<struct NullJoint>> joints;
    std::vector<std::unique_ptr<struct NullBuoyancy>> buoyancyVolumes;
    std::vector<std::unique_ptr<struct NullField>> forceFields;
    bool released = false;
};

/// Internal body record. The null backend integrates manually, so it keeps
/// everything the contract needs.
struct NullBody : Body
{
    BodyDesc desc;
    core::vec3 position{0.0f, 0.0f, 0.0f};
    core::vec3 rotation{0.0f, 0.0f, 0.0f};
    core::vec3 linearVelocity{0.0f, 0.0f, 0.0f};
    core::vec3 angularVelocity{0.0f, 0.0f, 0.0f};
    core::vec3 accumulatedForce{0.0f, 0.0f, 0.0f};
    core::vec3 accumulatedImpulse{0.0f, 0.0f, 0.0f};
    bool released = false;
};

struct NullJoint : Joint
{
    JointDesc desc;
    bool released = false;
};

struct NullBuoyancy : BuoyancyVolume
{
    BuoyancyVolumeDesc desc;
    bool released = false;
};

struct NullField : ForceField
{
    ForceFieldDesc desc;
    bool released = false;
};

/// A body's world-space centre of mass. The null backend integrates the
/// centre of mass directly, so an axis-aligned overlap test on the centre is
/// enough for buoyancy and force fields.
bool pointInside(const core::vec3& p, const core::vec3& centre, const core::vec3& extents)
{
    return std::fabs(p.x - centre.x) <= extents.x * 0.5f
        && std::fabs(p.y - centre.y) <= extents.y * 0.5f
        && std::fabs(p.z - centre.z) <= extents.z * 0.5f;
}

}  // namespace

struct NullPhysicsBackend::Impl
{
    std::vector<std::unique_ptr<NullWorld>> worlds;
    bool shutdown = false;
    std::uint64_t violations = 0;

    void countViolation() { ++violations; }

    bool ready() const { return !shutdown; }

    NullWorld* worldOf(World* world)
    {
        for (auto& w : worlds)
        {
            if (w.get() == world)
                return w.get();
        }
        return nullptr;
    }

    NullBody* bodyOf(Body* body)
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

NullPhysicsBackend::NullPhysicsBackend()
    : m_impl(std::make_unique<Impl>())
{
}

NullPhysicsBackend::~NullPhysicsBackend() = default;

std::string NullPhysicsBackend::describe() const
{
    return "null (headless, deterministic manual integration)";
}

std::uint32_t NullPhysicsBackend::capabilityFlags() const
{
    // The null backend implements the classical layer only: no cloth, no
    // soft body, no 2D-only restriction.
    return 0u;
}

void NullPhysicsBackend::shutdown()
{
    if (m_impl->shutdown)
    {
        m_impl->countViolation();
        return;
    }
    m_impl->shutdown = true;
}

std::uint64_t NullPhysicsBackend::violationCount() const noexcept
{
    return m_impl->violations;
}

World* NullPhysicsBackend::createWorld(const WorldConfig& config)
{
    std::string message;
    if (!config.validate(message))
        throw std::invalid_argument(message);

    auto record = std::make_unique<NullWorld>();
    record->config = config;
    World* world = record.get();
    m_impl->worlds.push_back(std::move(record));
    return world;
}

void NullPhysicsBackend::releaseWorld(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    NullWorld* record = m_impl->worldOf(world);
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

Body* NullPhysicsBackend::createBody(World* world, const BodyDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    NullWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto body = std::make_unique<NullBody>();
    body->desc = desc;
    body->position = desc.position;
    Body* handle = body.get();
    record->bodies.push_back(std::move(body));
    return handle;
}

void NullPhysicsBackend::releaseBody(World* world, Body* body)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    if (!m_impl->worldOf(world))
    {
        m_impl->countViolation();
        return;
    }
    NullBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->released = true;
}

void NullPhysicsBackend::applyForce(Body* body, const core::vec3& force)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    NullBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->accumulatedForce = b->accumulatedForce + force;
}

void NullPhysicsBackend::applyImpulse(Body* body, const core::vec3& impulse)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    NullBody* b = m_impl->bodyOf(body);
    if (!b || b->released)
    {
        m_impl->countViolation();
        return;
    }
    b->accumulatedImpulse = b->accumulatedImpulse + impulse;
}

void NullPhysicsBackend::step(World* world)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    NullWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        return;
    }

    const float dt = record->config.fixedStepSeconds;
    const core::vec3& gravity = record->config.gravity;

    for (auto& b : record->bodies)
    {
        if (b->released || b->desc.kind != BodyKind::Dynamic)
            continue;

        // Impulses are a step change in momentum: applied once, before the
        // integration, and cleared with the forces.
        b->linearVelocity = b->linearVelocity + b->accumulatedImpulse / b->desc.massKg;

        core::vec3 acceleration = b->desc.affectedByGravity ? gravity : core::vec3{};
        acceleration = acceleration + b->accumulatedForce / b->desc.massKg;

        // Buoyancy: Archimedes' force on the body's centre of mass, plus
        // drag proportional to the velocity inside the fluid.
        for (const auto& v : record->buoyancyVolumes)
        {
            if (v->released)
                continue;
            if (!pointInside(b->position, v->desc.centre, v->desc.extents))
                continue;
            const float volume = v->desc.extents.x * v->desc.extents.y
                               * v->desc.extents.z;
            const float displacedMass =
                v->desc.densityKgPerM3 * volume * b->desc.massKg;
            acceleration = acceleration + core::vec3{0.0f, 1.0f, 0.0f}
                * (displacedMass * 9.81f / b->desc.massKg);
            acceleration = acceleration - b->linearVelocity * v->desc.drag;
        }

        // Force fields: uniform acceleration inside the box.
        for (const auto& f : record->forceFields)
        {
            if (f->released)
                continue;
            if (pointInside(b->position, f->desc.centre, f->desc.extents))
                acceleration = acceleration + f->desc.acceleration;
        }

        // Semi-implicit Euler: v += a*dt, then x += v*dt.
        b->linearVelocity = b->linearVelocity + acceleration * dt;
        b->position = b->position + b->linearVelocity * dt;
        b->rotation = b->rotation + b->angularVelocity * dt;

        b->accumulatedForce = core::vec3{};
        b->accumulatedImpulse = core::vec3{};
    }
}

RigidBodyState NullPhysicsBackend::readState(Body* body) const
{
    for (const auto& w : m_impl->worlds)
    {
        for (const auto& b : w->bodies)
        {
            if (b.get() == body)
            {
                RigidBodyState state;
                state.position = b->position;
                state.rotation = b->rotation;
                state.linearVelocity = b->linearVelocity;
                state.angularVelocity = b->angularVelocity;
                return state;
            }
        }
    }
    return RigidBodyState{};
}

Joint* NullPhysicsBackend::createJoint(World* world, const JointDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    NullWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    // Body indices are world-creation order; both must exist.
    if (desc.bodyA >= record->bodies.size()
        || desc.bodyB >= record->bodies.size())
    {
        throw std::invalid_argument("joint body index out of range");
    }

    auto joint = std::make_unique<NullJoint>();
    joint->desc = desc;
    Joint* handle = joint.get();
    record->joints.push_back(std::move(joint));
    return handle;
}

void NullPhysicsBackend::releaseJoint(World* world, Joint* joint)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    NullWorld* record = m_impl->worldOf(world);
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
            return;
        }
    }
    m_impl->countViolation();
}

BuoyancyVolume* NullPhysicsBackend::createBuoyancyVolume(
    World* world, const BuoyancyVolumeDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    NullWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto volume = std::make_unique<NullBuoyancy>();
    volume->desc = desc;
    BuoyancyVolume* handle = volume.get();
    record->buoyancyVolumes.push_back(std::move(volume));
    return handle;
}

void NullPhysicsBackend::releaseBuoyancyVolume(World* world,
                                               BuoyancyVolume* volume)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    NullWorld* record = m_impl->worldOf(world);
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

ForceField* NullPhysicsBackend::createForceField(World* world,
                                                 const ForceFieldDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
        throw std::invalid_argument(message);

    NullWorld* record = m_impl->worldOf(world);
    if (!record || record->released)
    {
        m_impl->countViolation();
        throw std::invalid_argument("world is released or unknown");
    }

    auto field = std::make_unique<NullField>();
    field->desc = desc;
    ForceField* handle = field.get();
    record->forceFields.push_back(std::move(field));
    return handle;
}

void NullPhysicsBackend::releaseForceField(World* world, ForceField* field)
{
    if (!m_impl->ready())
    {
        m_impl->countViolation();
        return;
    }
    NullWorld* record = m_impl->worldOf(world);
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
