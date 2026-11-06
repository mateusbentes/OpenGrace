#pragma once

#include <cstdint>
#include <vector>

#include "grace/core/math/mat4.hpp"
#include "grace/core/math/vec3.hpp"
#include "grace/render/camera.hpp"
#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/render_target.hpp"

namespace grace::render {

class RenderBackend;

/// Maximum number of cascades. N is chosen as 4 by default: it matches the
/// per-object dynamic light budget scale of the original's forward path and
/// keeps the shader uniform arrays small. Counts are clamped to
/// [1, kMaxCascadeCount].
inline constexpr std::uint32_t kMaxCascadeCount = 4;

/// Scene bounding volume for the single-cascade pass (milestone 4b).
struct SceneBounds
{
    core::vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 10.0f;  ///< world units, must be > 0
};

/// Orthographic light-space view-projection matrix for a directional light
/// (the sun), covering the given scene bounds (single cascade, milestone 4b).
///
/// Convention: the light view places the eye at center - dir * (near + radius),
/// looking at the center, so the near plane touches the light-side extreme of
/// the bounding sphere; the orthographic frustum is
/// (-radius, radius, -radius, radius, near, far) in the OpenGL clip convention
/// (depth [-1, 1]). To cover the whole bounding sphere, far must be
/// >= near + 2 * radius (documented).
///
/// Throws std::invalid_argument when the sun direction is degenerate, the
/// radius is not positive, near is negative, or far <= near.
core::mat4 sunLightSpaceMatrix(const DirectionalLight& sun, const SceneBounds& bounds,
                               float near, float far);

// --- Cascaded shadow mapping (milestone 4c) --------------------------------

/// Configuration of the cascaded sun shadow pass.
struct CascadeConfig
{
    std::uint32_t count = 4;        ///< cascade count, clamped to [1, kMaxCascadeCount]
    std::uint32_t mapSize = 1024;   ///< fixed, documented per-cascade resolution
    float splitLambda = 0.5f;       ///< practical split scheme mix in [0, 1]
    float bias = 0.005f;            ///< constant light-space [0,1] depth bias
    float filterTolerance = 0.01f;  ///< bilateral depth-comparison tolerance, [0,1] depth
    bool debug = false;             ///< visualise the cascade index (off by default)
};

/// A rendered cascaded shadow map: per-cascade depth targets, light-space
/// matrices and view-space split depths. Plain data, no graphics-API types;
/// attach it to a DrawSubmission to have submit() apply it to the sun term.
struct ShadowMap
{
    std::vector<RenderTarget*> targets;       ///< backend-owned handles
    std::vector<core::mat4> lightMatrices;    ///< per cascade
    std::vector<float> splitDepths;           ///< cascadeCount + 1 view-space depths
    std::uint32_t cascadeCount = 0;
    std::uint32_t mapSize = 0;
    float bias = 0.005f;
    float filterTolerance = 0.01f;
    bool debug = false;
};

/// Throws std::invalid_argument on invalid config values (mapSize == 0,
/// splitLambda outside [0, 1], negative bias, non-positive filterTolerance).
/// The cascade count is clamped to [1, kMaxCascadeCount], not rejected.
void validateCascadeConfig(const CascadeConfig& config);

/// Computes the cascade split depths for the camera range [near, far] using
/// the documented practical split scheme (a mix of logarithmic and uniform
/// splits, Zhang et al.):
///
///   split_i = lambda * near * (far/near)^(i/N)
///           + (1 - lambda) * (near + (far - near) * i/N)
///
/// Returns count + 1 values: [0] = near, [count] = far, monotonic non-decreasing
/// and clamped into [near, far]. Derived from the camera parameters only.
///
/// Degenerate inputs (documented): if far - near is tiny, splits may coincide
/// but stay monotonic; near <= 0 or far <= near throws std::invalid_argument;
/// count is clamped to [1, kMaxCascadeCount] (an N larger than the useful
/// split count simply collapses to the maximum).
std::vector<float> computeCascadeSplits(float near, float far, std::uint32_t count,
                                        float lambda);

/// The 8 world-space corners of the camera frustum slice between view-space
/// depths splitNear and splitFar (documented: derived from CameraParams).
std::vector<core::vec3> cascadeSliceCorners(const CameraParams& camera, float splitNear,
                                            float splitFar);

/// Trapezoidal light-space fitting for one cascade slice: transforms the
/// slice's 8 frustum corners into light space (light view oriented along the
/// sun direction) and fits the orthographic box to their min/max, so the
/// covered area in light space is minimised — rather than fitting the whole
/// view frustum as the single-cascade pass does. The depth range is the
/// light-space z extent of the corners (near = farthest corner distance from
/// the light eye, far = nearest), expanded by a small documented epsilon so
/// the range is never degenerate.
///
/// Throws std::invalid_argument on degenerate input (invalid sun/camera,
/// splitFar <= splitNear).
core::mat4 fitCascadeLightMatrix(const DirectionalLight& sun, const CameraParams& camera,
                                 float splitNear, float splitFar);

/// Renders the submitted draw list into `config.count` depth-only cascade
/// targets from the sun's point of view, using generic render-pass
/// composition through the backend contract (beginRenderPass / clear / draw /
/// endRenderPass) — the contract does not know what a shadow is. The depth
/// shader is the standard position-only depth pass.
///
/// Returns the ShadowMap; the caller releases the targets through
/// backend.releaseRenderTarget when done. Throws std::invalid_argument on
/// invalid sun/camera/config/submissions.
ShadowMap renderShadowCascades(RenderBackend& backend, const DirectionalLight& sun,
                               const CameraParams& camera,
                               const std::vector<DrawSubmission>& submissions,
                               const CascadeConfig& config);

}  // namespace grace::render
