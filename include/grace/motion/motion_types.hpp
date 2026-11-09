#pragma once
// The motion module's data types: skeletons, clips, tracks, events,
// blendshapes and the pose the evaluation produces (docs/grace-architecture
// .md section 3.7).
//
// The module is plain data, deliberately: the evaluation is deterministic
// CPU work (the null backend of the scene module is the same pattern), so
// the same clip and the same time produce the same pose every run. Nothing
// here names a graphics API, a physics SDK or a scripting language.
//
// The bone budgets the documented architecture declares (32 bones per
// object on mobile, 8192 on high-end) are data quantities, not limits the
// module enforces: the skeleton validates its own structure, and the
// engine's budget is the application's choice.
#include <cstdint>
#include <string>
#include <vector>
#include "grace/core/math/quaternion.hpp"
#include "grace/core/math/vec3.hpp"
namespace grace {
namespace motion {
/// A bone's bind description: its name, its parent's index (the root has
/// no parent) and its rest transform in the parent's space.
struct BoneDesc final
{
    std::string name;
    /// The parent's index, or -1 for a root bone.
    std::int32_t parent = -1;
    core::vec3 restPosition{0.0f, 0.0f, 0.0f};
    core::quat restRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool validate(std::string& message) const;
};
/// A skeleton's description: the bone list in topological order (a
/// parent's index is always lower than its own).
struct SkeletonDesc final
{
    std::string name;
    std::vector<BoneDesc> bones;
    bool validate(std::string& message) const;
};
/// A position key: the time in seconds and the value in the parent's
/// space. Keys are sorted by time, ascending.
struct VecKey final
{
    float time = 0.0f;
    core::vec3 value{0.0f, 0.0f, 0.0f};
};
/// A rotation key: the time in seconds and the value in the parent's
/// space. Keys are sorted by time, ascending.
struct QuatKey final
{
    float time = 0.0f;
    core::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};
/// One bone's track within a clip: the bone's index and the key lists.
/// A bone with no track keeps the rest pose.
struct BoneTrack final
{
    std::uint32_t boneIndex = 0;
    std::vector<VecKey> positions;
    std::vector<QuatKey> rotations;
};
/// A clip event: the event's name, the action string and the time in
/// seconds. The documented architecture calls them hotspots and actions;
/// the name is the hotspot, the action is the payload the application
/// dispatches.
struct ClipEvent final
{
    std::string name;
    std::string action;
    float time = 0.0f;
};
/// A clip's description: the name, the duration in seconds, the tracks
/// and the events. A loop clip wraps its time; a non-loop one clamps.
struct ClipDesc final
{
    std::string name;
    float duration = 0.0f;
    bool loop = false;
    std::vector<BoneTrack> tracks;
    std::vector<ClipEvent> events;
    bool validate(std::string& message) const;
};
/// A blendshape's description: the name, the vertex indices the shape
/// moves and the offsets it applies. The weight is the instance's
/// current weight, 0 by default.
struct BlendShapeDesc final
{
    std::string name;
    std::vector<std::uint32_t> vertexIndices;
    std::vector<core::vec3> offsets;
    float weight = 0.0f;
    bool validate(std::string& message) const;
};
/// The evaluated skeleton state: the local positions and rotations per
/// bone, in the skeleton's bone order, plus the blendshape weights.
struct Pose final
{
    std::vector<core::vec3> positions;
    std::vector<core::quat> rotations;
    std::vector<float> shapeWeights;
};
}  // namespace motion
}  // namespace grace
