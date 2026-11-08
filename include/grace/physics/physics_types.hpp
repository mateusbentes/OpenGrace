#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "grace/core/math/vec3.hpp"

namespace grace::physics {

/// Rigid body mobility. A static body never moves; a dynamic body is
/// integrated by the world's fixed step.
enum class BodyKind : std::uint8_t
{
    Static,
    Dynamic
};

/// Collision shape. Every shape is a primitive: the classical layer of the
/// engine does not need meshes, and the three backends behind the contract
/// all implement these natively.
struct ShapeDesc
{
    enum class Kind : std::uint8_t
    {
        Box,
        Sphere,
        Capsule
    };

    Kind kind = Kind::Box;
    /// Box half extents (metres). Used only for Kind::Box.
    core::vec3 boxHalfExtents{0.5f, 0.5f, 0.5f};
    /// Sphere radius (metres). Used only for Kind::Sphere.
    float sphereRadius = 0.5f;
    /// Capsule radius (metres). Used only for Kind::Capsule.
    float capsuleRadius = 0.5f;
    /// Capsule cylinder half height (metres). Used only for Kind::Capsule.
    float capsuleHalfHeight = 0.5f;

    /// Rejects a shape that cannot exist in a world. Returns false and fills
    /// the message with the first violation found.
    bool validate(std::string& message) const;
};

/// Description of a rigid body, created through the backend.
struct BodyDesc
{
    /// Body mobility. A static body ignores forces and gravity.
    BodyKind kind = BodyKind::Static;
    /// Collision shape. Must pass ShapeDesc::validate().
    ShapeDesc shape;
    /// World position of the body's centre of mass (metres).
    core::vec3 position{0.0f, 0.0f, 0.0f};
    /// Mass (kilograms). Must be > 0 for dynamic bodies; static bodies
    /// ignore it. A dynamic body without a mass is rejected.
    float massKg = 1.0f;
    /// Linear damping factor (per second, 0 = no damping).
    float linearDamping = 0.0f;
    /// Angular damping factor (per second, 0 = no damping).
    float angularDamping = 0.0f;
    /// Whether the body is affected by gravity.
    bool affectedByGravity = true;

    /// Rejects a body that cannot exist in a world. Returns false and fills
    /// the message with the first violation found.
    bool validate(std::string& message) const;
};

/// Joint constraint kinds. The classical layer requires distance joints and
/// point-to-point constraints as a minimum (architecture section 3.6).
struct JointDesc
{
    enum class Kind : std::uint8_t
    {
        /// Keeps the distance between two bodies' anchor points.
        Distance,
        /// Pins two bodies' anchor points together.
        PointToPoint
    };

    Kind kind = Kind::Distance;
    /// Indices of the two bodies in the owning world, in the order the
    /// application created them. The backend validates them.
    std::uint32_t bodyA = 0;
    std::uint32_t bodyB = 0;
    /// Anchor points in each body's local space (metres).
    core::vec3 anchorA{0.0f, 0.0f, 0.0f};
    core::vec3 anchorB{0.0f, 0.0f, 0.0f};
    /// Rest length for Kind::Distance (metres). Must be >= 0.
    float restLength = 0.0f;

    /// Rejects a joint that cannot exist in a world. Returns false and fills
    /// the message with the first violation found.
    bool validate(std::string& message) const;
};

/// Axis-aligned box of fluid. Bodies inside it are displaced by buoyancy
/// (architecture section 3.6).
struct BuoyancyVolumeDesc
{
    /// Centre of the box in world space (metres).
    core::vec3 centre{0.0f, 0.0f, 0.0f};
    /// Full extents of the box (metres). Must be > 0 on every axis.
    core::vec3 extents{1.0f, 1.0f, 1.0f};
    /// Fluid density (kilograms per cubic metre). Must be > 0.
    float densityKgPerM3 = 1000.0f;
    /// Velocity damping applied to bodies inside the fluid (per second).
    float drag = 0.5f;

    /// Rejects a volume that cannot exist in a world. Returns false and
    /// fills the message with the first violation found.
    bool validate(std::string& message) const;
};

/// Axis-aligned box of uniform acceleration. Bodies inside it are
/// accelerated by the field (architecture section 3.6).
struct ForceFieldDesc
{
    /// Centre of the box in world space (metres).
    core::vec3 centre{0.0f, 0.0f, 0.0f};
    /// Full extents of the box (metres). Must be > 0 on every axis.
    core::vec3 extents{1.0f, 1.0f, 1.0f};
    /// Uniform acceleration inside the field (metres per second squared).
    core::vec3 acceleration{0.0f, 0.0f, 0.0f};

    /// Rejects a field that cannot exist in a world. Returns false and
    /// fills the message with the first violation found.
    bool validate(std::string& message) const;
};

/// World configuration: gravity and the fixed step the backend consumes.
struct WorldConfig
{
    /// Constant downward acceleration (metres per second squared). A
    /// negative y value is gravity as usual; an all-zero value is allowed.
    core::vec3 gravity{0.0f, -9.81f, 0.0f};
    /// Fixed step size in seconds (the engine's fixed-step clock). Must be
    /// > 0 and finite.
    float fixedStepSeconds = 1.0f / 60.0f;

    /// Rejects a world that cannot exist. Returns false and fills the
    /// message with the first violation found.
    bool validate(std::string& message) const;
};

/// Readback of a body's world state after stepping. Orientation is the
/// body's rotation, in radians, applied in X, then Y, then Z order (the
/// same order the engine's Transform uses).
struct RigidBodyState
{
    core::vec3 position{0.0f, 0.0f, 0.0f};
    /// Euler angles in radians, X, then Y, then Z order.
    core::vec3 rotation{0.0f, 0.0f, 0.0f};
    /// Linear velocity (metres per second).
    core::vec3 linearVelocity{0.0f, 0.0f, 0.0f};
    /// Angular velocity (radians per second).
    core::vec3 angularVelocity{0.0f, 0.0f, 0.0f};
};

/// Opaque world handle. The application owns the world's lifetime through
/// the backend; bodies, joints and fields live inside it.
class World
{
public:
    virtual ~World() = default;
};

/// Opaque rigid body handle. Released through the backend; a body must be
/// released before its world is.
class Body
{
public:
    virtual ~Body() = default;
};

/// Opaque joint handle. Released through the backend; a joint must be
/// released before its world is.
class Joint
{
public:
    virtual ~Joint() = default;
};

/// Opaque buoyancy volume handle. Released through the backend.
class BuoyancyVolume
{
public:
    virtual ~BuoyancyVolume() = default;
};

/// Opaque force field handle. Released through the backend.
class ForceField
{
public:
    virtual ~ForceField() = default;
};

}  // namespace grace::physics
