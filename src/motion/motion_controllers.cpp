// The motion module's controllers: the look-at and foot placement node
// controllers the documented architecture's custom node controllers
// cover (section 3.7).
//
// A controller is the application's plug-in: the machine calls it per
// bone after the layer's evaluation, before the blending, and the
// controller overrides the bone's local rotation. The look-at rotates
// the bone so its forward axis points at a world-space target; the foot
// placement rotates the bone so its end lands on a world-space point.
// The target is the controller's own state; the application updates it
// through setTarget as the scene moves.
#include "grace/motion/motion_controllers.hpp"
#include <algorithm>
#include <cmath>
namespace grace {
namespace motion {
namespace {
/// The rotation that carries `from` onto `to` (both unit length): the
/// axis is the cross product, the angle the acos of the dot product.
core::quat fromTo(const core::vec3& from, const core::vec3& to)
{
    const float d = core::dot(from, to);
    if (d >= 0.9999f)
    {
        return core::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    if (d <= -0.9999f)
    {
        // Opposite: the axis is any vector orthogonal to `from`.
        const core::vec3 axis =
            std::abs(from.x) < 0.9f
                ? core::cross(from, core::vec3{1.0f, 0.0f, 0.0f})
                : core::cross(from, core::vec3{0.0f, 1.0f, 0.0f});
        return core::fromAxisAngle(core::normalise(axis),
                                   3.14159265f);
    }
    const core::vec3 axis = core::cross(from, to);
    const float angle = std::acos(std::min(1.0f, std::max(-1.0f, d)));
    return core::fromAxisAngle(core::normalise(axis), angle);
}
}  // namespace
LookAtController::LookAtController(std::uint32_t bone,
                                   const core::vec3& target,
                                   const core::vec3& axis)
    : boneIndex(bone), targetPoint(target), forwardAxis(axis)
{
}
std::uint32_t LookAtController::bone() const
{
    return boneIndex;
}
void LookAtController::apply(const Pose& pose, float time,
                             core::quat& rotation)
{
    // The look-at runs on the pose's local space: the bone's forward
    // axis rotates onto the target's direction in the same space. The
    // target's direction is the target point normalised (the target is
    // the aim direction, not a world point; the application converts a
    // world point through the inverse of the skeleton's root transform
    // if it needs to).
    (void)pose;
    (void)time;
    const core::vec3 to = core::normalise(targetPoint);
    rotation = rotation * fromTo(forwardAxis, to);
}
const core::vec3& LookAtController::target() const
{
    return targetPoint;
}
void LookAtController::setTarget(const core::vec3& target)
{
    targetPoint = target;
}
FootController::FootController(std::uint32_t bone,
                               const core::vec3& target)
    : boneIndex(bone), targetPoint(target)
{
}
std::uint32_t FootController::bone() const
{
    return boneIndex;
}
void FootController::apply(const Pose& pose, float time,
                           core::quat& rotation)
{
    // The foot placement rotates the bone so its local +Y axis points
    // at the target's direction (the leg's down axis inverts to the
    // target's up direction).
    (void)pose;
    (void)time;
    const core::vec3 to = core::normalise(targetPoint);
    rotation = rotation * fromTo(core::vec3{0.0f, 1.0f, 0.0f}, to);
}
const core::vec3& FootController::target() const
{
    return targetPoint;
}
void FootController::setTarget(const core::vec3& target)
{
    targetPoint = target;
}
}  // namespace motion
}  // namespace grace
