#pragma once

#include <cmath>

#include "grace/core/math/contract.hpp"
#include "grace/core/math/mat4.hpp"
#include "grace/core/math/vec3.hpp"

namespace grace::core {

/// Right-handed view matrix ("look at"): the camera sits at eye and looks at
/// target, with up as the approximate up direction. Maps world space to view
/// space where the camera looks down -Z. A point at target ends up at view
/// coordinates (0, 0, -distance, 1).
inline mat4 lookAt(const vec3& eye, const vec3& target, const vec3& up)
{
    const vec3 forward = normalise(target - eye);
    const vec3 rightRaw = cross(forward, up);
    if (length(rightRaw) == 0.0f)
        math::detail::degenerateInput("lookAt", "up vector parallel to the view direction");
    const vec3 right = normalise(rightRaw);
    const vec3 realUp = cross(right, forward);

    mat4 m = mat4::identity();
    m(0, 0) = right.x;
    m(1, 0) = right.y;
    m(2, 0) = right.z;
    m(0, 1) = realUp.x;
    m(1, 1) = realUp.y;
    m(2, 1) = realUp.z;
    m(0, 2) = -forward.x;
    m(1, 2) = -forward.y;
    m(2, 2) = -forward.z;
    m(3, 0) = -dot(right, eye);
    m(3, 1) = -dot(realUp, eye);
    m(3, 2) = dot(forward, eye);
    return m;
}

/// Analytic inverse of perspective() (same clip convention). Used by the
/// deferred lighting pass to reconstruct view/world positions from the depth
/// buffer. Throws std::invalid_argument on degenerate parameters.
inline mat4 perspectiveInverse(float fovY, float aspect, float near, float far)
{
    if (!(fovY > 0.0f) || !(aspect > 0.0f) || !(near > 0.0f) || !(far > near))
        throw std::invalid_argument("perspectiveInverse: degenerate parameters");
    const float f = 1.0f / static_cast<float>(std::tan(fovY * 0.5f));
    mat4 m{};
    m(0, 0) = aspect / f;
    m(1, 1) = 1.0f / f;
    m(3, 2) = -1.0f;  // row 2, column 3: z' = -w
    m(2, 3) = (near - far) / (2.0f * far * near);  // row 3, column 2
    m(3, 3) = (far + near) / (2.0f * far * near);  // row 3, column 3
    return m;
}

/// Orthographic projection matrix in the OpenGL clip convention: after the
/// perspective divide (w = 1 here), x/y are in [-1, 1] and z (depth) is in
/// [-1, 1], with -1 at the near plane and +1 at the far plane. Used for the
/// sun's light-space shadow matrix.
inline mat4 orthographic(float left, float right, float bottom, float top, float near,
                         float far)
{
    mat4 m{};
    m(0, 0) = 2.0f / (right - left);
    m(1, 1) = 2.0f / (top - bottom);
    m(2, 2) = -2.0f / (far - near);
    m(3, 0) = -(right + left) / (right - left);
    m(3, 1) = -(top + bottom) / (top - bottom);
    m(3, 2) = -(far + near) / (far - near);
    m(3, 3) = 1.0f;
    return m;
}

/// Perspective projection matrix in the OpenGL clip convention: after the
/// perspective divide, x/y are in [-1, 1] and z (depth) is in [-1, 1], with
/// -1 at the near plane and +1 at the far plane.
///
/// fovY is the vertical field of view in radians (documented convention:
/// vertical, not horizontal, not diagonal). aspect is width / height.
/// near and far are positive distances from the eye; near < far is required.
inline mat4 perspective(float fovY, float aspect, float near, float far)
{
    const float f = 1.0f / static_cast<float>(std::tan(fovY * 0.5f));
    mat4 m{};
    m(0, 0) = f / aspect;
    m(1, 1) = f;
    m(2, 2) = (far + near) / (near - far);
    m(3, 2) = (2.0f * far * near) / (near - far);
    m(2, 3) = -1.0f;
    return m;
}

}  // namespace grace::core
