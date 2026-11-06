#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "grace/core/math/mat4.hpp"
#include "grace/render/light.hpp"
#include "grace/render/material.hpp"
#include "grace/render/mesh.hpp"

namespace grace::render {

struct ShadowMap;        // defined in grace/render/shadow.hpp
struct PointShadowMaps;  // defined in grace/render/shadow.hpp

/// The documented per-object dynamic light limit: the original engine's
/// forward path supports "up to four lights per object plus sunlight"
/// (docs/grace-architecture.md section 3.3). Enforced exactly; the sun is not
/// counted against it.
inline constexpr std::uint32_t kMaxDynamicLightsPerObject = 4;

/// One draw submission: a mesh, a transform, surface parameters, an optional
/// sun (directional light, handled separately) and the dynamic lights.
/// Plain data, no graphics-API types — this is not a scene graph.
struct DrawSubmission
{
    const GpuMesh* mesh = nullptr;
    core::mat4 model = core::mat4::identity();  ///< model transform
    Material material;                           ///< carries the surface parameters
    std::optional<DirectionalLight> sun;         ///< handled separately from the dynamic lights
    std::vector<DynamicLight> lights;            ///< at most kMaxDynamicLightsPerObject
    const ShadowMap* shadow = nullptr;           ///< optional cascaded sun shadow map (4c)
    const PointShadowMaps* pointShadows = nullptr;  ///< optional paraboloid maps (4d)
    /// How many leading point lights in `lights` request a shadow map. More
    /// than kMaxShadowCastingPointLights is not rejected: the excess lights
    /// are lit WITHOUT a shadow map (documented), and submit() counts a
    /// protocol violation for the over-budget request so it is never silent.
    std::uint32_t shadowLightRequests = 0;

    /// Throws std::invalid_argument with a specific message when the
    /// submission is invalid: null mesh, more than
    /// kMaxDynamicLightsPerObject dynamic lights (explicitly rejected, never
    /// silently truncated), or any invalid light/surface parameter.
    void validate() const;

    /// Same, but with an explicit dynamic-light limit. The deferred path
    /// (milestone 5) uses kMaxDeferredLights instead of the per-object limit;
    /// the forward path keeps kMaxDynamicLightsPerObject (documented).
    void validate(std::uint32_t maxDynamicLights) const;
};

}  // namespace grace::render
