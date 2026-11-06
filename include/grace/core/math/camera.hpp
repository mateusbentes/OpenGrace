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
