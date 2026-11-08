#pragma once

#include <memory>
#include <string>

#include "grace/physics/physics_backend.hpp"

namespace grace::physics {

/// PhysX 3.4 physics backend (8c). Maps the classical contract onto the
/// PhysX 3.4 SDK: rigid bodies, shapes, forces, gravity, distance and
/// point-to-point joints, buoyancy and force fields. The "extra physics"
/// the original GRACE 2 engine called out (CPU cloth and soft body) is a
/// capability of this backend only: it is reported through
/// capabilityFlags() (decision 32), and no Px* type leaves this module.
class PhysXPhysicsBackend : public PhysicsBackend
{
public:
    PhysXPhysicsBackend();
    ~PhysXPhysicsBackend() override;

    PhysXPhysicsBackend(const PhysXPhysicsBackend&) = delete;
    PhysXPhysicsBackend& operator=(const PhysXPhysicsBackend&) = delete;
    PhysXPhysicsBackend(PhysXPhysicsBackend&&) = delete;
    PhysXPhysicsBackend& operator=(PhysXPhysicsBackend&&) = delete;

    std::string describe() const override;
    std::uint32_t capabilityFlags() const override;
    void shutdown() override;
    std::uint64_t violationCount() const noexcept override;

    World* createWorld(const WorldConfig& config) override;
    void releaseWorld(World* world) override;
    Body* createBody(World* world, const BodyDesc& desc) override;
    void releaseBody(World* world, Body* body) override;
    void applyForce(Body* body, const core::vec3& force) override;
    void applyImpulse(Body* body, const core::vec3& impulse) override;
    void step(World* world) override;
    RigidBodyState readState(Body* body) const override;

    Joint* createJoint(World* world, const JointDesc& desc) override;
    void releaseJoint(World* world, Joint* joint) override;

    BuoyancyVolume* createBuoyancyVolume(World* world,
                                         const BuoyancyVolumeDesc& desc) override;
    void releaseBuoyancyVolume(World* world, BuoyancyVolume* volume) override;
    ForceField* createForceField(World* world, const ForceFieldDesc& desc) override;
    void releaseForceField(World* world, ForceField* field) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace grace::physics
