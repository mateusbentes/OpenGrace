#pragma once

#include <cstdint>

#include "grace/core/math/mat4.hpp"
#include "grace/core/math/vec3.hpp"

namespace grace::render {

/// Dual paraboloid shadow mapping (milestone 4d), plain math in the render
/// module so it is testable without a GPU.
///
/// Coordinate convention (documented): light space has the light at the
/// origin looking along **+Z**. The front hemisphere is d.z > 0 (the
/// half-space in front of the light), the back hemisphere d.z < 0. The seam is
/// the circle d.z == 0; paraboloidProject assigns it to the front hemisphere.
///
/// Projection (front):  uv = ( d.x,  d.y) / (1 + d.z)
/// Projection (back):   uv = (-d.x, -d.y) / (1 - d.z)
/// with uv in [-1, 1]^2 (the unit disk; scale by 0.5 + 0.5 for texture
/// coordinates). The inverse maps uv back to a unit direction:
///   front: z = (1 - r^2) / (1 + r^2),  d = (u, v, z) * (1 + z)
///   back:  z = (r^2 - 1) / (r^2 + 1),  d = (-u, -v, z) * (1 - z)
/// where r^2 = u^2 + v^2.
///
/// Seam and singularity (documented): at the seam (r = 1) both hemispheres
/// give d.z = 0 and the two maps meet; each fragment uses exactly one map
/// chosen by the sign of d.z, so there is no gap and no double coverage. The
/// singularity behind the light is d = -Z (the back pole): it is valid in the
/// back map (uv 0,0) and undefined in the front map (division by 1 + d.z =
/// 0). The depth pass therefore clips geometry with d.z <= 0 in the front
/// pass and d.z >= 0 in the back pass, so nothing is ever looked up in the
/// wrong hemisphere.

enum class ParaboloidHemisphere : std::uint8_t
{
    Front = 0,  ///< d.z >= 0 in light space
    Back = 1    ///< d.z < 0 in light space
};

/// Paraboloid texture coordinate: hemisphere plus uv in [-1, 1]^2.
struct ParaboloidCoord
{
    ParaboloidHemisphere hemisphere;
    float u;
    float v;
};

/// Projects a direction (light space, need not be unit length) to paraboloid
/// uv. Throws std::invalid_argument on a zero-length direction.
ParaboloidCoord paraboloidProject(const core::vec3& direction);

/// Inverse of paraboloidProject: hemisphere + uv in [-1, 1]^2 -> unit
/// direction. Throws std::invalid_argument for uv outside the unit disk
/// (r^2 > 1), which has no direction.
core::vec3 paraboloidDirection(ParaboloidHemisphere hemisphere, float u, float v);

/// World -> light space matrix for a point light at lightPos looking along
/// forward: light-space axes are (right, up, forward) so light-space +Z is the
/// light's forward direction and the front hemisphere is what the light
/// illuminates ahead of it. Throws std::invalid_argument on a degenerate
/// forward/up.
core::mat4 paraboloidLightView(const core::vec3& lightPos, const core::vec3& forward,
                               const core::vec3& up);

}  // namespace grace::render
