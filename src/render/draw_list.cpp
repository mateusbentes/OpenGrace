#include "grace/render/draw_list.hpp"

#include <stdexcept>
#include <string>

#include "grace/render/shadow.hpp"

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
}

}  // namespace grace::render
