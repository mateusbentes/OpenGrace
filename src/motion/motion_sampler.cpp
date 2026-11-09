// The motion module's sampler: the skeleton, the clips, the blendshapes
// and the deterministic evaluation of the poses.
//
// The sampler is deterministic CPU work: the same clip and the same time
// produce the same pose every run. The key interpolation lerps the
// positions and slerps the rotations (the quaternion's slerp, the short
// arc), the FK pass composes the world matrices from the local pose, and
// the events fire in (from, to].
#include "grace/motion/motion_sampler.hpp"
#include <algorithm>
#include <cmath>
namespace grace {
namespace motion {
MotionError::MotionError(const std::string& message)
    : std::runtime_error(message)
{
}
namespace {
/// The quaternion's axis-angle form: the axis and the angle the
/// rotation() helper composes the matrix from. A unit quaternion's
/// angle is 2 * acos(w); the axis is the vector part normalised.
void axisAngle(const core::quat& q, core::vec3& axis, float& angle)
{
    const float w = std::min(1.0f, std::max(-1.0f, q.w));
    angle = 2.0f * std::acos(w);
    const float sinHalf = std::sqrt(1.0f - w * w);
    if (sinHalf <= 0.0f)
    {
        axis = core::vec3{0.0f, 0.0f, 1.0f};
        angle = 0.0f;
        return;
    }
    axis = core::vec3{q.x / sinHalf, q.y / sinHalf, q.z / sinHalf};
}
/// The local matrix of a pose's position and rotation.
core::mat4 localMatrix(const core::vec3& position,
                       const core::quat& rotation)
{
    core::vec3 axis;
    float angle = 0.0f;
    axisAngle(rotation, axis, angle);
    return core::translation(position) * core::rotation(axis, angle);
}
}  // namespace
struct Sampler::Impl
{
    SkeletonDesc skeleton;
    std::vector<ClipDesc> clips;
    std::vector<BlendShapeDesc> shapes;
    /// Finds the segment the time falls in: the index i with keys[i].time
    /// <= t < keys[i + 1].time. A time before the first key clamps to the
    /// segment 0; a time at or past the last key clamps to the last key.
    template <typename Key>
    static std::size_t segment(const std::vector<Key>& keys, float t)
    {
        std::size_t i = 0;
        while (i + 1 < keys.size() && keys[i + 1].time <= t)
        {
            ++i;
        }
        return i;
    }
    /// The position at t: the lerp between the segment's keys.
    static core::vec3 samplePosition(const std::vector<VecKey>& keys,
                                     float t)
    {
        if (keys.empty())
        {
            return core::vec3{0.0f, 0.0f, 0.0f};
        }
        const std::size_t i = segment(keys, t);
        if (i + 1 == keys.size())
        {
            return keys[i].value;
        }
        const VecKey& a = keys[i];
        const VecKey& b = keys[i + 1];
        const float span = b.time - a.time;
        const float s = span > 0.0f ? (t - a.time) / span : 0.0f;
        return a.value + (b.value - a.value) * s;
    }
    /// The rotation at t: the slerp between the segment's keys.
    static core::quat sampleRotation(const std::vector<QuatKey>& keys,
                                     float t)
    {
        if (keys.empty())
        {
            return core::quat{1.0f, 0.0f, 0.0f, 0.0f};
        }
        const std::size_t i = segment(keys, t);
        if (i + 1 == keys.size())
        {
            return keys[i].value;
        }
        const QuatKey& a = keys[i];
        const QuatKey& b = keys[i + 1];
        const float span = b.time - a.time;
        const float s = span > 0.0f ? (t - a.time) / span : 0.0f;
        return core::slerp(a.value, b.value, s);
    }
    /// The pose from one clip at t: every bone's local state.
    static Pose samplePose(const Impl& self, const ClipDesc& clip,
                           float t)
    {
        Pose pose;
        const std::size_t boneCount = self.skeleton.bones.size();
        pose.positions.assign(boneCount, core::vec3{0.0f, 0.0f, 0.0f});
        pose.rotations.assign(boneCount,
                              core::quat{1.0f, 0.0f, 0.0f, 0.0f});
        for (const auto& track : clip.tracks)
        {
            if (track.boneIndex >= boneCount)
            {
                continue;
            }
            pose.positions[track.boneIndex] =
                samplePosition(track.positions, t);
            pose.rotations[track.boneIndex] =
                sampleRotation(track.rotations, t);
        }
        pose.shapeWeights.reserve(self.shapes.size());
        for (const auto& shape : self.shapes)
        {
            pose.shapeWeights.push_back(shape.weight);
        }
        return pose;
    }
};
Sampler::Sampler() : impl(new Impl)
{
}
Sampler::~Sampler() = default;
void Sampler::setSkeleton(const SkeletonDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
    {
        throw MotionError(message);
    }
    impl->skeleton = desc;
    // The clip tracks index the bones; the skeleton's change invalidates
    // them. The clips stay: the application re-adds them if it re-sets
    // the skeleton.
    impl->clips.clear();
    impl->shapes.clear();
}
const SkeletonDesc& Sampler::skeleton() const
{
    return impl->skeleton;
}
void Sampler::addClip(const ClipDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
    {
        throw MotionError(message);
    }
    impl->clips.push_back(desc);
}
std::size_t Sampler::clipCount() const
{
    return impl->clips.size();
}
const ClipDesc& Sampler::clip(std::uint32_t clipId) const
{
    if (clipId >= impl->clips.size())
    {
        throw MotionError("a clip id out of range");
    }
    return impl->clips[clipId];
}
void Sampler::addBlendShape(const BlendShapeDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
    {
        throw MotionError(message);
    }
    impl->shapes.push_back(desc);
}
std::size_t Sampler::shapeCount() const
{
    return impl->shapes.size();
}
void Sampler::setShapeWeight(std::uint32_t shapeId, float weight)
{
    if (shapeId >= impl->shapes.size())
    {
        throw MotionError("a blendshape id out of range");
    }
    impl->shapes[shapeId].weight = weight;
}
Pose Sampler::sample(std::uint32_t clipId, float time) const
{
    if (clipId >= impl->clips.size())
    {
        throw MotionError("a clip id out of range");
    }
    const ClipDesc& clip = impl->clips[clipId];
    const float t = wrapTime(clip, time);
    return Impl::samplePose(*impl, clip, t);
}
std::vector<core::mat4> Sampler::fkPose(const Pose& pose) const
{
    const std::size_t boneCount = impl->skeleton.bones.size();
    std::vector<core::mat4> world(boneCount);
    for (std::size_t i = 0; i < boneCount; ++i)
    {
        const BoneDesc& bone = impl->skeleton.bones[i];
        const core::mat4 bind =
            localMatrix(bone.restPosition, bone.restRotation);
        const core::mat4 local =
            localMatrix(pose.positions[i], pose.rotations[i]);
        // The bone's world matrix: the parent's world, then the bind
        // (the rest transform), then the local pose's offset.
        world[i] = bone.parent >= 0
            ? world[static_cast<std::size_t>(bone.parent)] * bind * local
            : bind * local;
    }
    return world;
}
void Sampler::blendPose(Pose& out, const Pose& other, float t)
{
    const std::size_t count = std::min(out.positions.size(),
                                       other.positions.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        out.positions[i] =
            out.positions[i] + (other.positions[i] - out.positions[i]) * t;
        out.rotations[i] =
            core::slerp(out.rotations[i], other.rotations[i], t);
    }
    const std::size_t shapes = std::min(out.shapeWeights.size(),
                                        other.shapeWeights.size());
    for (std::size_t i = 0; i < shapes; ++i)
    {
        out.shapeWeights[i] =
            out.shapeWeights[i]
            + (other.shapeWeights[i] - out.shapeWeights[i]) * t;
    }
}
std::vector<ClipEvent> Sampler::eventsAt(std::uint32_t clipId, float from,
                                         float to) const
{
    if (clipId >= impl->clips.size())
    {
        throw MotionError("a clip id out of range");
    }
    std::vector<ClipEvent> fired;
    const ClipDesc& clip = impl->clips[clipId];
    for (const auto& event : clip.events)
    {
        bool hit = false;
        if (from <= to)
        {
            hit = event.time > from && event.time <= to;
        }
        else
        {
            // The loop wrap: the events in (from, duration] and
            // [0, to] fire.
            hit = event.time > from || event.time <= to;
        }
        if (hit)
        {
            fired.push_back(event);
        }
    }
    return fired;
}
float Sampler::wrapTime(const ClipDesc& clip, float time)
{
    if (time < 0.0f)
    {
        return 0.0f;
    }
    if (clip.loop)
    {
        return std::fmod(time, clip.duration);
    }
    return std::min(time, clip.duration);
}
}  // namespace motion
}  // namespace grace
