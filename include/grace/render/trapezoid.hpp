#pragma once

#include <vector>

#include "grace/core/math/mat4.hpp"
#include "grace/core/math/vec2.hpp"
#include "grace/core/math/vec3.hpp"
#include "grace/render/camera.hpp"
#include "grace/render/light.hpp"

namespace grace::render {

/// Trapezoidal cascade construction (milestone 4e), plain math in the render
/// module so it is testable without a GPU.
///
/// Construction (documented): the slice's 8 frustum corners are transformed
/// into light space (light view along the sun direction, looking down -Z, so
/// "near face" = the 4 corners with the largest light-space z). The trapezoid
/// is the quad whose near edge runs between (nearMid - nearHalf, nearMidY)
/// and (nearMid + nearHalf, nearMidY) and whose far edge likewise, where
/// nearMid/farMid are the midpoints of the near/far face corners in light-space
/// xy and nearHalf/farHalf are their maximum |x - mid| extents. This is a
/// trapezoid (near and far edges may differ in length and offset) whenever the
/// camera is not looking straight along the light direction.
///
/// The projective warp is the 2D homography mapping those 4 trapezoid corners
/// to the unit square corners (-1,-1), (1,-1), (1,1), (-1,1). Composed as
/// W * orthographic(fitted box) * lightView, the resulting matrix is genuinely
/// projective: its bottom row is NOT the identity row.

/// The four trapezoid corners in light-space 2D: near edge (left, right),
/// far edge (left, right).
struct Trapezoid
{
    core::vec2 nearLeft;
    core::vec2 nearRight;
    core::vec2 farLeft;
    core::vec2 farRight;
};

/// Builds the trapezoid from the slice's 8 light-space corners (x, y used;
/// z selects near/far faces). Returns false when the construction is
/// degenerate (collapsed quad, e.g. camera looking straight along the light
/// direction) — the caller then falls back to the fitted orthographic matrix.
bool trapezoidFromLightSpaceCorners(const std::vector<core::vec3>& lightSpaceCorners,
                                    Trapezoid& out);

/// 2D homography (3x3, up to scale) mapping src[0..3] to dst[0..3] (the unit
/// square corners in order). Returns false when the system is singular.
/// Solved as a linear system in the 8 unknowns with the bottom-right entry
/// fixed to 1 (Gaussian elimination with partial pivoting, plain floats).
bool homographyFromQuad(const core::vec2 src[4], const core::vec2 dst[4], float h[9]);

/// Full trapezoidal cascade light-space matrix: W * ortho(box) * lightView,
/// where the box tightly covers the trapezoid (plus a small documented margin)
/// and W is the quad->square homography embedded so that z passes through and
/// w becomes the homography denominator (projective bottom row).
/// Falls back to the fitted orthographic matrix when the trapezoid is
/// degenerate (documented); in that case *fellBack is set to true.
core::mat4 fitTrapezoidalCascadeLightMatrix(const DirectionalLight& sun,
                                            const CameraParams& camera, float splitNear,
                                            float splitFar, bool* fellBack);

}  // namespace grace::render
