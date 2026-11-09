#pragma once
// The motion module's sampler: the skeleton, the clips and the
// deterministic evaluation of the poses (docs/grace-architecture.md
// section 3.7).
//
// The sampler is plain data, deliberately: the evaluation is deterministic
// CPU work, so the same clip and the same time produce the same pose every
// run. The skinning stays hardware-side (the render module owns the skin
// matrices); the sampler produces the local pose and the forward-kinematics
// matrices the application can hand to the render side. Nothing here names
// a graphics API, a physics SDK or a scripting language.
#include <memory>
#include <stdexcept>
#include <vector>
#include "grace/core/math/mat4.hpp"
#include "grace/motion/motion_types.hpp"
namespace grace {
namespace motion {
/// The motion module's error: the throws the evaluation reports. The
/// message is the module's own words, and the failure is a build error,
/// not a runtime fallback.
class MotionError : public std::runtime_error
{
public:
    explicit MotionError(const std::string& message);
};
/// The sampler: the skeleton, the clips, the blendshapes and the
/// deterministic evaluation of the poses.
class Sampler
{
public:
    Sampler();
    ~Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    /// Sets the skeleton. Throws MotionError on an invalid skeleton (a
    /// non-topological order, an unknown parent index, a duplicate name).
    void setSkeleton(const SkeletonDesc& desc);
    /// The skeleton in use.
    const SkeletonDesc& skeleton() const;
    /// Adds a clip. Throws MotionError on an invalid clip (a non-positive
    /// duration, a non-topological track order, a non-sorted key list).
    void addClip(const ClipDesc& desc);
    /// The number of clips.
    std::size_t clipCount() const;
    /// The clip by id (the order the clips were added).
    const ClipDesc& clip(std::uint32_t clipId) const;
    /// Adds a blendshape. Throws MotionError on an invalid shape (a
    /// non-empty name, a vertex index and offset count mismatch).
    void addBlendShape(const BlendShapeDesc& desc);
    /// The number of blendshapes.
    std::size_t shapeCount() const;
    /// Sets a blendshape's current weight (the instance's value, 0 by
    /// default).
    void setShapeWeight(std::uint32_t shapeId, float weight);
    /// The pose at the clip's time: the local positions and rotations per
    /// bone, and the blendshape weights. The time wraps on a loop clip
    /// and clamps on a non-loop one.
    Pose sample(std::uint32_t clipId, float time) const;
    /// The world transforms per bone (the skinning matrices) from a pose.
    /// The skeleton's bone order is the matrix order.
    std::vector<core::mat4> fkPose(const Pose& pose) const;
    /// Blends `other` into `out` by the weight t: 0 keeps `out`, 1 keeps
    /// `other`. The positions lerp, the rotations slerp, the shape
    /// weights lerp. Both poses need the same bone count.
    static void blendPose(Pose& out, const Pose& other, float t);
    /// The events that fired between two times in a clip: the events in
    /// (from, to]. On a loop wrap the events in (from, duration] and
    /// [0, to] fire.
    std::vector<ClipEvent> eventsAt(std::uint32_t clipId, float from,
                                    float to) const;
    /// The wrapped clip time: a loop clip wraps by the duration, a
    /// non-loop one clamps to [0, duration].
    static float wrapTime(const ClipDesc& clip, float time);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace motion
}  // namespace grace
