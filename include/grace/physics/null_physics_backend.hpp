#pragma once

#include <memory>
#include <string>
#include <vector>

#include "grace/physics/physics_backend.hpp"
#include "grace/physics/physics_types.hpp"

namespace grace::physics {

/// Null physics backend: a headless implementation of the classical contract
/// with manual integration (semi-implicit Euler at the world's fixed step).
/// It exists so that deterministic tests can run without a physics SDK
/// linked, and so that application code can be written against the contract
/// before a real backend lands.
class NullPhysicsBackend : public PhysicsBackend
{
public:
    NullPhysicsBackend();
    ~NullPhysicsBackend() override;

    NullPhysicsBackend(const NullPhysicsBackend&) = delete;
    NullPhysicsBackend& operator=(const NullPhysicsBackend&) = delete;
    NullPhysicsBackend(NullPhysicsBackend&&) = delete;
    NullPhysicsBackend& operator=(NullPhysicsBackend&&) = delete;

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
