#pragma once

#include <cmath>

#include "grace/core/math/contract.hpp"
#include "grace/core/math/vec3.hpp"

namespace grace::core {

/// Unit quaternion (w, x, y, z) for free rotations. Scalar implementation, no
/// intrinsics. The identity is w=1. Conjugate and inverse are the same for a
/// unit quaternion; the quaternion is kept unit length on the operations that
/// must preserve it (fromAxisAngle, slerp).
struct quat
{
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr quat() = default;
    constexpr quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}
};

/// Rotation about an arbitrary axis (which need not be normalised, but must
/// not be zero) by angle radians, right-hand rule. Not constexpr in C++17
/// (std::sin/std::cos are not constexpr). A zero axis is a contract violation
/// in Debug builds through detail::degenerateInput, the same rule rotation()
/// in mat4.hpp applies.
inline quat fromAxisAngle(const vec3& axis, float angle)
{
    if (length(axis) == 0.0f)
        math::detail::degenerateInput("fromAxisAngle", "zero-length axis");
    const vec3 a = normalise(axis);
    const float s = static_cast<float>(std::sin(angle * 0.5f));
    return quat{static_cast<float>(std::cos(angle * 0.5f)),
                a.x * s,
                a.y * s,
                a.z * s};
}

/// Quaternion product (a * b applies b, then a). Not unit-length preserving
/// in general; callers that need it re-normalise.
constexpr quat operator*(const quat& a, const quat& b)
{
    return quat{a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

/// Rotates a vector by the quaternion: v' = q * v * q^-1.
constexpr vec3 rotate(const quat& q, const vec3& v)
{
    const vec3 qv{q.x, q.y, q.z};
    // v' = v + 2*w*cross(qv, v) + 2*cross(qv, cross(qv, v)).
    const vec3 c1 = cross(qv, v);
    const vec3 c2 = cross(qv, c1);
    return v + c1 * (2.0f * q.w) + c2 * 2.0f;
}

constexpr bool operator==(const quat& a, const quat& b)
{
    return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
}
constexpr bool operator!=(const quat& a, const quat& b) { return !(a == b); }

/// Euclidean length of the quaternion.
inline float length(const quat& q)
{
    return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

/// Scales the quaternion to unit length. A zero quaternion normalises to the
/// zero quaternion (documented Release behaviour), the same rule
/// normalise(vec3) applies; in Debug builds a zero input is reported through
/// detail::degenerateInput.
inline quat normalise(const quat& q)
{
    const float len = length(q);
    if (len == 0.0f)
    {
        math::detail::degenerateInput("normalise(quat)", "zero-length quaternion");
        return quat{0.0f, 0.0f, 0.0f, 0.0f};
    }
    return quat{q.w / len, q.x / len, q.y / len, q.z / len};
}

/// Spherical interpolation of two unit quaternions. The inputs need
/// not be exactly unit length; the result is re-normalised. When a and
/// b are nearly equal the fallback is the nlerp form, so t = 0 returns
/// a and t = 1 returns b regardless.
inline quat slerp(const quat& a, const quat& b, float t)
{
    float cosOmega =
        a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    quat target = b;
    if (cosOmega < 0.0f)
    {
        // The short arc: negate one input when the dot product is
        // negative, which keeps the path on the shorter sphere side.
        cosOmega = -cosOmega;
        target = quat{-b.w, -b.x, -b.y, -b.z};
    }
    float k0 = 1.0f - t;
    float k1 = t;
    if (cosOmega < 0.9995f)
    {
        const float omega =
            static_cast<float>(std::acos(static_cast<double>(cosOmega)));
        const float sinOmega = std::sin(omega);
        k0 = std::sin((1.0f - t) * omega) / sinOmega;
        k1 = std::sin(t * omega) / sinOmega;
    }
    return normalise(quat{a.w * k0 + target.w * k1,
                          a.x * k0 + target.x * k1,
                          a.y * k0 + target.y * k1,
                          a.z * k0 + target.z * k1});
}

}  // namespace grace::core
