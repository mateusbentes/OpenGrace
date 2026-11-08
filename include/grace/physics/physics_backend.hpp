#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "grace/core/math/vec3.hpp"
#include "grace/physics/physics_types.hpp"

namespace grace::physics {

class World;
class Body;
class Joint;
class BuoyancyVolume;
class ForceField;

/// Platform-neutral physics contract.
///
/// The contract is the classical-physics layer of the GRACE 2 engine: rigid
/// bodies, collision shapes, forces, gravity, joints, buoyancy and force
/// fields. It is the plugin abstraction the original engine was explicitly
/// built on (PhysX, Bullet, Box2D behind one abstraction), and it stays free
/// of any physics-API type: no Px*, bt* or b2* types may appear here.
///
/// The "extra physics" (the PhysX 3.4 CPU cloth and soft-body capability the
/// original engine called extra physics) is NOT part of this contract: it is
/// a backend capability only the PhysX backend exposes, reported through
/// capabilityFlags() (decision 32).
///
/// Units: SI (metres, kilograms, seconds, radians). The world is Y-up.
///
/// Protocol: mutating calls made before a backend is initialised (backends
/// with an explicit initialise step) or after shutdown() are protocol
/// violations and are counted through violationCount(); a repeated
/// shutdown() is a violation too but must stay safe.
class PhysicsBackend
{
public:
    virtual ~PhysicsBackend() = default;

    /// Human-readable backend description, for example "null (headless)".
    virtual std::string describe() const = 0;

    // -- Capability flags -----------------------------------------------------
    //
    // Bit flags for what a backend actually implements. The classical layer
    // (rigid bodies, forces, gravity, joints, buoyancy, force fields) is
    // required of every backend; the extra layer (cloth, soft body) is a
    // backend capability, reported truthfully so that the application can
    // query it before submitting anything.

    /// Bit 0: CPU cloth (extra physics). Only the PhysX backend sets it.
    static constexpr std::uint32_t kCapabilityCloth = 1u << 0;
    /// Bit 1: CPU soft body (extra physics). Only the PhysX backend sets it.
    static constexpr std::uint32_t kCapabilitySoftBody = 1u << 1;
    /// Bit 2: 2D-only backend (Box2D). Its classical layer is flat; callers
    /// must keep z=0. A 2D backend reports no cloth/soft body.
    static constexpr std::uint32_t kCapability2DOnly = 1u << 2;

    /// Bit flags for what the backend implements (decision 32).
    virtual std::uint32_t capabilityFlags() const = 0;

    /// Releases backend resources. Exactly one successful call per backend
    /// lifetime; calls after shutdown are protocol violations.
    virtual void shutdown() = 0;

    /// Number of protocol violations observed so far.
    virtual std::uint64_t violationCount() const noexcept = 0;

    // -- World and bodies ------------------------------------------------------
    //
    // The world is explicit, not a singleton: the application creates a world
    // through the backend, and all bodies, joints and fields live inside it.
    // The application owns the world lifetime: releaseWorld() destroys it and
    // every body it owns. A body must be released before its world is.

    /// Creates a world. Throws std::invalid_argument on an invalid config.
    virtual World* createWorld(const WorldConfig& config) = 0;

    /// Releases a world. Use after release is a protocol violation.
    virtual void releaseWorld(World* world) = 0;

    /// Creates a rigid body in the world. Throws std::invalid_argument on an
    /// invalid desc.
    virtual Body* createBody(World* world, const BodyDesc& desc) = 0;

    /// Releases a body. Use after release is a protocol violation.
    virtual void releaseBody(World* world, Body* body) = 0;

    /// Accumulates a force at the body's centre of mass for the next step.
    /// Forces are cleared after each step.
    virtual void applyForce(Body* body, const core::vec3& force) = 0;

    /// Accumulates an impulse at the body's centre of mass, applied as a
    /// step change in momentum. Impulses are cleared after each step.
    virtual void applyImpulse(Body* body, const core::vec3& impulse) = 0;

    // -- Stepping and readback -------------------------------------------------
    //
    // The classical GRACE 2 fixed-step clock produces the fixed step count;
    // the backend consumes it. step() advances the world by exactly one
    // fixed step; the application drives it from the clock.

    /// Advances the world by exactly one fixed step (dt = the world's
    /// configured fixedStepSeconds). Forces and impulses are cleared after
    /// each step. Calls after shutdown are protocol violations.
    virtual void step(World* world) = 0;

    /// Reads back the world transform of a body after stepping.
    virtual RigidBodyState readState(Body* body) const = 0;

    // -- Joints ----------------------------------------------------------------
    //
    // A joint constrains two bodies or one body and the world. Created with
    // an explicit desc; the backend owns the handle. The classical layer
    // requires distance joints and point-to-point constraints as a minimum.

    /// Creates a joint in the world. Throws std::invalid_argument on an
    /// invalid desc (unknown type, unknown or released bodies).
    virtual Joint* createJoint(World* world, const JointDesc& desc) = 0;

    /// Releases a joint. Use after release is a protocol violation.
    virtual void releaseJoint(World* world, Joint* joint) = 0;

    // -- Buoyancy and force fields --------------------------------------------
    //
    // The classical layer of the GRACE 2 engine included buoyancy and force
    // fields (architecture section 3.6). These are per-world objects, not
    // per-body: the application creates them once, and every step applies
    // them to each body that overlaps them.

    /// Creates a buoyancy volume (an axis-aligned box of fluid). Throws
    /// std::invalid_argument on an invalid desc (zero size, density <= 0).
    virtual BuoyancyVolume* createBuoyancyVolume(World* world,
                                                 const BuoyancyVolumeDesc& desc) = 0;

    /// Releases a buoyancy volume. Use after release is a protocol violation.
    virtual void releaseBuoyancyVolume(World* world, BuoyancyVolume* volume) = 0;

    /// Creates a force field (an axis-aligned box of uniform acceleration).
    /// Throws std::invalid_argument on an invalid desc (zero size).
    virtual ForceField* createForceField(World* world, const ForceFieldDesc& desc) = 0;

    /// Releases a force field. Use after release is a protocol violation.
    virtual void releaseForceField(World* world, ForceField* field) = 0;
};

}  // namespace grace::physics
