#pragma once

#include "grace/core/math/mat4.hpp"
#include "grace/core/math/vec3.hpp"

namespace grace::core {

/// TRS transform: position, Euler rotation (radians) and scale.
struct Transform
{
    vec3 position{0.0f, 0.0f, 0.0f};
    /// Euler angles in radians, applied in X, then Y, then Z order.
    vec3 rotation{0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f, 1.0f, 1.0f};

    /// Composes the model matrix T * Rz * Ry * Rx * S.
    mat4 compose() const
    {
        const mat4 t = grace::core::translation(position);
        const mat4 rx = grace::core::rotation(vec3{1.0f, 0.0f, 0.0f}, rotation.x);
        const mat4 ry = grace::core::rotation(vec3{0.0f, 1.0f, 0.0f}, rotation.y);
        const mat4 rz = grace::core::rotation(vec3{0.0f, 0.0f, 1.0f}, rotation.z);
        const mat4 s = grace::core::scale(this->scale);
        return t * rz * ry * rx * s;
    }
};

}  // namespace grace::core
