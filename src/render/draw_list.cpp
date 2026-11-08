#include "grace/render/draw_list.hpp"

#include <stdexcept>
#include <string>

#include "grace/render/shadow.hpp"

namespace grace::render {

void DrawSubmission::validate() const
{
    validate(kMaxDynamicLightsPerObject);
}

void DrawSubmission::validate(std::uint32_t maxDynamicLights) const
{
    if (mesh == nullptr)
        throw std::invalid_argument("draw submission: null mesh");
    if (lights.size() > maxDynamicLights)
        throw std::invalid_argument(
            "draw submission: " + std::to_string(lights.size()) +
            " dynamic lights exceeds the limit of " + std::to_string(maxDynamicLights) +
            "; rejected, not truncated");
    if (sun.has_value())
        sun->validate();
    for (const DynamicLight& light : lights)
        validateLight(light);
    validateSurfaceParams(material.surfaceParams());
    if (shadow != nullptr)
    {
        if (shadow->cascadeCount == 0 || shadow->cascadeCount > kMaxCascadeCount)
            throw std::invalid_argument("draw submission: invalid shadow cascade count");
        if (shadow->targets.size() != shadow->cascadeCount ||
            shadow->lightMatrices.size() != shadow->cascadeCount ||
            shadow->splitDepths.size() != shadow->cascadeCount + 1u)
            throw std::invalid_argument("draw submission: inconsistent shadow map arrays");
    }
    if (pointShadows != nullptr)
    {
        if (pointShadows->entries.size() > kMaxShadowCastingPointLights)
            throw std::invalid_argument(
                "draw submission: more point shadow map entries than the budget of " +
                std::to_string(kMaxShadowCastingPointLights));
        for (const PointShadowMapEntry& entry : pointShadows->entries)
        {
            if (entry.lightIndex >= lights.size())
                throw std::invalid_argument(
                    "draw submission: point shadow entry light index out of range");
            if (entry.front == nullptr || entry.back == nullptr)
                throw std::invalid_argument("draw submission: point shadow entry without maps");
        }
    }
    if (volume != nullptr)
    {
        if (volume->texture == nullptr)
            throw std::invalid_argument("draw submission: light volume without a texture");
        if (volume->resolution == 0)
            throw std::invalid_argument("draw submission: light volume resolution must be > 0");
        if (!(volume->extent.x > 0.0f) || !(volume->extent.y > 0.0f) ||
            !(volume->extent.z > 0.0f))
            throw std::invalid_argument(
                "draw submission: light volume extent components must be > 0");
    }
}

}  // namespace grace::render
