#pragma once
// The motion module's controllers: the look-at and foot placement node
// controllers the documented architecture's custom node controllers
// cover (section 3.7).
//
// A controller is the application's plug-in: the machine calls it per
// bone after the layer's evaluation, before the blending, and the
// controller overrides the bone's local rotation. The look-at and the
// foot placement are the two the architecture names; the application
// can subclass the base for its own.
#include <memory>
#include "grace/core/math/mat4.hpp"
#include "grace/core/math/quaternion.hpp"
#include "grace/core/math/vec3.hpp"
#include "grace/core/math/vec3.hpp"
#include "grace/motion/motion_machine.hpp"
namespace grace {
namespace motion {
/// The controller base: the machine calls a controller per bone after
/// the layer's evaluation, before the blending, and the controller
/// overrides the bone's local rotation. The application subclasses it
/// for its own node controllers; the look-at and the foot placement
/// are the two the documented architecture names.
class BoneController
{
public:
    BoneController() = default;
    virtual ~BoneController() = default;
    BoneController(const BoneController&) = delete;
    BoneController& operator=(const BoneController&) = delete;
    /// The bone's index the controller drives.
    virtual std::uint32_t bone() const = 0;
    /// Overrides the bone's local rotation. The pose is the layer's
    /// evaluated pose before the override; the time is the layer's
    /// clip time.
    virtual void apply(const Pose& pose, float time,
                       core::quat& rotation) = 0;
};
/// Look-at: the bone aims at a world-space target. The controller
/// rotates the bone's local rotation so the bone's forward axis points
/// at the target, in the pose's world space (the bone's world transform
/// from the FK pass).
class LookAtController final : public BoneController
{
public:
    /// The bone index, the world-space target point and the bone's
    /// forward axis in the bone's local space (defaults to +Z).
    LookAtController(std::uint32_t boneIndex, const core::vec3& target,
                     const core::vec3& forwardAxis = {0.0f, 0.0f, 1.0f});
    std::uint32_t bone() const override;
    /// Rotates the bone's local rotation so the bone's forward axis
    /// points at the target. The target is the same world-space point
    /// the controller was constructed with; the application updates the
    /// target through target() to move the aim over time.
    void apply(const Pose& pose, float time, core::quat& rotation) override;
    /// The world-space target point.
    const core::vec3& target() const;
    /// Sets the world-space target point.
    void setTarget(const core::vec3& target);
private:
    std::uint32_t boneIndex = 0;
    core::vec3 targetPoint;
    core::vec3 forwardAxis;
};
/// Foot placement: the bone's world position snaps to a world-space
/// target. The controller rotates the bone's local rotation so the
/// bone's world position lands on the target point (the bone's world
/// position from the FK pass).
class FootController final : public BoneController
{
public:
    /// The bone index and the world-space target point.
    FootController(std::uint32_t boneIndex, const core::vec3& target);
    std::uint32_t bone() const override;
    /// Rotates the bone's local rotation so the bone's world position
    /// lands on the target point.
    void apply(const Pose& pose, float time, core::quat& rotation) override;
    /// The world-space target point.
    const core::vec3& target() const;
    /// Sets the world-space target point.
    void setTarget(const core::vec3& target);
private:
    std::uint32_t boneIndex = 0u;
    core::vec3 targetPoint;
};
}  // namespace motion
}  // namespace grace
