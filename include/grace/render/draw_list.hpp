#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "grace/core/math/mat4.hpp"
#include "grace/render/light.hpp"
#include "grace/render/material.hpp"
#include "grace/render/mesh.hpp"
#include "grace/render/render_target.hpp"

namespace grace::render {

struct ShadowMap;        // defined in grace/render/shadow.hpp
struct PointShadowMaps;  // defined in grace/render/shadow.hpp

/// The documented per-object dynamic light limit: the original engine's
/// forward path supports "up to four lights per object plus sunlight"
/// (docs/grace-architecture.md section 3.3). Enforced exactly; the sun is not
/// counted against it.
inline constexpr std::uint32_t kMaxDynamicLightsPerObject = 4;

/// The frame's light volume, as plain data (milestone 7, fourth increment:
/// the compose). The renderer owns the volume and hands it to the lighting
/// pass the same way the shadow maps are handed over: plain data with a
/// backend-owned handle, no graphics-API types. The lighting pass samples
/// the volume at the fragment's world position and adds albedo * radiance
/// to the ambient term (decision 17(d)).
struct LightVolume
{
    TextureView* texture = nullptr;  ///< backend-owned volume texture handle
    core::vec3 origin;               ///< world-space minimum corner
    core::vec3 extent;               ///< world-space size, each component > 0
    std::uint32_t resolution = 0;    ///< texels along each axis, > 0
};

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
    /// When true, the submission only casts shadows (it feeds the shadow
    /// passes) and is not drawn (documented; used for occluders).
    bool castOnly = false;
    /// Scene-global ambient term in linear units, filled in by the renderer
    /// from RendererConfig::ambient (default 0 here keeps direct-backend
    /// behaviour unchanged). Documented deviation: not a documented light type.
    float ambient = 0.0f;
    const ShadowMap* shadow = nullptr;           ///< optional cascaded sun shadow map (4c)
    const PointShadowMaps* pointShadows = nullptr;  ///< optional paraboloid maps (4d)
    /// Optional light volume (milestone 7, fourth increment): when set, the
    /// lighting pass adds albedo * radiance to the ambient term (decision
    /// 17(d)). Filled by the renderer from the frame's light volume; the
    /// compose flag is off by default, so a frame that does not ask for it
    /// is unchanged.
    const LightVolume* volume = nullptr;
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
