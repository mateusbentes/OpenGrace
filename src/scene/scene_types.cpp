// The scene module's data types: the validation the entity and the LOD
// descriptions carry.
#include "grace/scene/scene_types.hpp"
namespace grace {
namespace scene {
bool EntityDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "scene: an empty entity name is invalid";
        return false;
    }
    if (scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f)
    {
        message = "scene: a zero scale is invalid";
        return false;
    }
    for (std::size_t i = 0; i < lods.size(); ++i)
    {
        if (i > 0 && !(lods[i - 1].distance < lods[i].distance))
        {
            message = "scene: LOD distances must be strictly ascending";
            return false;
        }
    }
    return true;
}
}  // namespace scene
}  // namespace grace
