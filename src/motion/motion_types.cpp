// The motion module's data types: the validation of the bone, the clip,
// the blendshape and the layer descriptions.
#include "grace/motion/motion_types.hpp"
#include <set>
namespace grace {
namespace motion {
bool BoneDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "a bone's name is empty";
        return false;
    }
    if (parent < -1)
    {
        message = "a bone's parent index is below -1";
        return false;
    }
    return true;
}
bool SkeletonDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "a skeleton's name is empty";
        return false;
    }
    if (bones.empty())
    {
        message = "a skeleton's bone list is empty";
        return false;
    }
    std::set<std::string> names;
    for (std::size_t i = 0; i < bones.size(); ++i)
    {
        std::string boneMessage;
        if (!bones[i].validate(boneMessage))
        {
            message = "the bone " + std::to_string(i) + ": " + boneMessage;
            return false;
        }
        if (bones[i].parent >= static_cast<std::int32_t>(i))
        {
            message = "the bone " + std::to_string(i) + ": a parent "
                      "index is not lower than its own (topological)";
            return false;
        }
        if (!names.insert(bones[i].name).second)
        {
            message = "the bone " + std::to_string(i) + ": a duplicate "
                      "name";
            return false;
        }
    }
    return true;
}
bool ClipDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "a clip's name is empty";
        return false;
    }
    if (duration <= 0.0f)
    {
        message = "a clip's duration is not positive";
        return false;
    }
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i].positions.empty() && tracks[i].rotations.empty())
        {
            message = "the track " + std::to_string(i) + ": both key "
                      "lists are empty";
            return false;
        }
        for (std::size_t k = 1; k < tracks[i].positions.size(); ++k)
        {
            if (tracks[i].positions[k].time <=
                tracks[i].positions[k - 1].time)
            {
                message = "the track " + std::to_string(i) + ": the "
                          "position keys are not sorted by time";
                return false;
            }
        }
        for (std::size_t k = 1; k < tracks[i].rotations.size(); ++k)
        {
            if (tracks[i].rotations[k].time <=
                tracks[i].rotations[k - 1].time)
            {
                message = "the track " + std::to_string(i) + ": the "
                          "rotation keys are not sorted by time";
                return false;
            }
        }
    }
    return true;
}
bool BlendShapeDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "a blendshape's name is empty";
        return false;
    }
    if (vertexIndices.size() != offsets.size())
    {
        message = "a blendshape's vertex index and offset counts differ";
        return false;
    }
    return true;
}
}  // namespace motion
}  // namespace grace
