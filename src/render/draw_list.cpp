#include "grace/render/draw_list.hpp"

#include <stdexcept>
#include <string>

namespace grace::render {

void DrawSubmission::validate() const
{
    if (mesh == nullptr)
        throw std::invalid_argument("draw submission: null mesh");
    if (lights.size() > kMaxDynamicLightsPerObject)
        throw std::invalid_argument(
            "draw submission: " + std::to_string(lights.size()) +
            " dynamic lights exceeds the per-object limit of " +
            std::to_string(kMaxDynamicLightsPerObject) +
            " (docs/grace-architecture.md section 3.3); rejected, not truncated");
    if (sun.has_value())
        sun->validate();
    for (const DynamicLight& light : lights)
        validateLight(light);
    validateSurfaceParams(material.surfaceParams());
}

}  // namespace grace::render
