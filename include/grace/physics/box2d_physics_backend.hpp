#pragma once

#include <memory>
#include <string>

#include "grace/physics/physics_backend.hpp"

namespace grace::physics {

/// Box2D physics backend (8c). Maps the classical contract onto Box2D 3:
/// the flat layer of the classical physics (bodies, shapes, forces, gravity,
/// joints). The contract's z axis is flat for this backend: every body
/// created in a Box2D world keeps z=0, and the backend reports
/// kCapability2DOnly so the application can query it before submitting.
class Box2DPhysicsBackend : public PhysicsBackend
{
public:
    Box2DPhysicsBackend();
    ~Box2DPhysicsBackend() override;

    Box2DPhysicsBackend(const Box2DPhysicsBackend&) = delete;
    Box2DPhysicsBackend& operator=(const Box2DPhysicsBackend&) = delete;
    Box2DPhysicsBackend(Box2DPhysicsBackend&&) = delete;
    Box2DPhysicsBackend& operator=(Box2DPhysicsBackend&&) = delete;

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
