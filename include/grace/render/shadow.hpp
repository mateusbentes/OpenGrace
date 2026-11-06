#pragma once

#include "grace/core/math/mat4.hpp"
#include "grace/core/math/vec3.hpp"
#include "grace/render/light.hpp"

namespace grace::render {

/// Scene bounding volume for the sun shadow map: a bounding sphere. The
/// single-cascade shadow map covers this sphere (milestone 4b; trapezoidal
/// cascades are 4c and out of scope).
struct SceneBounds
{
    core::vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 10.0f;  ///< world units, must be > 0
};

/// Orthographic light-space view-projection matrix for a directional light
/// (the sun), covering the given scene bounds.
///
/// Convention: the light view places the eye at center - dir * (near + radius),
/// looking at the center, so the near plane touches the light-side extreme of
/// the bounding sphere and every scene point is in front of the camera; the
/// orthographic frustum is (-radius, radius, -radius, radius, near, far) in
/// the OpenGL clip convention (depth [-1, 1]). To cover the whole bounding
/// sphere, far must be >= near + 2 * radius (documented; smaller far values
/// clip the far side of the scene).
///
/// Throws std::invalid_argument when the sun direction is degenerate, the
/// radius is not positive, near is negative, or far <= near.
core::mat4 sunLightSpaceMatrix(const DirectionalLight& sun, const SceneBounds& bounds,
                               float near, float far);

}  // namespace grace::render
