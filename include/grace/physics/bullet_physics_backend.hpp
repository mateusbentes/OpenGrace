#pragma once

#include <memory>
#include <string>

#include "grace/physics/physics_backend.hpp"

namespace grace::physics {

/// Bullet physics backend (8c). Maps the classical contract onto Bullet 3:
/// bodies, shapes, forces, gravity, distance and point-to-point joints,
/// buoyancy and force fields. No bt* type leaves this module.
class BulletPhysicsBackend : public PhysicsBackend
{
public:
    BulletPhysicsBackend();
    ~BulletPhysicsBackend() override;

    BulletPhysicsBackend(const BulletPhysicsBackend&) = delete;
    BulletPhysicsBackend& operator=(const BulletPhysicsBackend&) = delete;
    BulletPhysicsBackend(BulletPhysicsBackend&&) = delete;
    BulletPhysicsBackend& operator=(BulletPhysicsBackend&&) = delete;

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
