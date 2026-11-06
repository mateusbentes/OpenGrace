#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "grace/core/math/vec3.hpp"
#include "grace/core/math/vec4.hpp"

namespace grace::core {

/// 4x4 float matrix in column-major storage, matching the OpenGL convention:
/// cols[c][r] is the element at column c, row r, so a mat4 can be handed to
/// glUniformMatrix4fv as-is.
struct mat4
{
    float cols[4][4] = {};

    constexpr float& operator()(std::uint32_t col, std::uint32_t row) { return cols[col][row]; }
    constexpr float operator()(std::uint32_t col, std::uint32_t row) const
    {
        return cols[col][row];
    }

    static constexpr mat4 identity()
    {
        mat4 m{};
        m(0, 0) = 1.0f;
        m(1, 1) = 1.0f;
        m(2, 2) = 1.0f;
        m(3, 3) = 1.0f;
        return m;
    }
};

/// Matrix product: (a * b)(c, r) = sum_k a(k, r) * b(c, k).
constexpr mat4 operator*(const mat4& a, const mat4& b)
{
    mat4 result{};
    for (std::uint32_t c = 0; c < 4; ++c)
        for (std::uint32_t r = 0; r < 4; ++r)
        {
            float sum = 0.0f;
            for (std::uint32_t k = 0; k < 4; ++k)
                sum += a(k, r) * b(c, k);
            result(c, r) = sum;
        }
    return result;
}

/// Matrix-vector product treating v as a column vector.
constexpr vec4 operator*(const mat4& m, const vec4& v)
{
    vec4 result{};
    for (std::uint32_t r = 0; r < 4; ++r)
        result[static_cast<std::size_t>(r)] =
            m(0, r) * v.x + m(1, r) * v.y + m(2, r) * v.z + m(3, r) * v.w;
    return result;
}

constexpr bool operator==(const mat4& a, const mat4& b)
{
    for (std::uint32_t c = 0; c < 4; ++c)
        for (std::uint32_t r = 0; r < 4; ++r)
            if (a(c, r) != b(c, r))
                return false;
    return true;
}

/// Translation matrix.
constexpr mat4 translation(const vec3& t)
{
    mat4 m = mat4::identity();
    m(3, 0) = t.x;
    m(3, 1) = t.y;
    m(3, 2) = t.z;
    return m;
}

/// Scale matrix.
constexpr mat4 scale(const vec3& s)
{
    mat4 m{};
    m(0, 0) = s.x;
    m(1, 1) = s.y;
    m(2, 2) = s.z;
    m(3, 3) = 1.0f;
    return m;
}

/// Rotation about an arbitrary axis (Rodrigues' formula). The axis need not
/// be normalised. angle is in radians, right-hand rule about the axis.
/// Not constexpr in C++17 (std::sin/std::cos are not constexpr).
inline mat4 rotation(const vec3& axis, float angle)
{
    const vec3 a = normalise(axis);
    const float c = static_cast<float>(std::cos(angle));
    const float s = static_cast<float>(std::sin(angle));
    const float t = 1.0f - c;
    mat4 m = mat4::identity();
    m(0, 0) = t * a.x * a.x + c;
    m(0, 1) = t * a.x * a.y + s * a.z;
    m(0, 2) = t * a.x * a.z - s * a.y;
    m(1, 0) = t * a.x * a.y - s * a.z;
    m(1, 1) = t * a.y * a.y + c;
    m(1, 2) = t * a.y * a.z + s * a.x;
    m(2, 0) = t * a.x * a.z + s * a.y;
    m(2, 1) = t * a.y * a.z - s * a.x;
    m(2, 2) = t * a.z * a.z + c;
    return m;
}

/// Inverse of an affine transform (upper 3x3 linear part plus translation).
/// Throws std::invalid_argument when the linear part is singular.
inline mat4 inverseAffine(const mat4& m)
{
    // Invert the upper-left 3x3 via the adjugate divided by the determinant.
    const float a = m(0, 0), b = m(1, 0), c = m(2, 0);
    const float d = m(0, 1), e = m(1, 1), f = m(2, 1);
    const float g = m(0, 2), h = m(1, 2), i = m(2, 2);
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det == 0.0f)
        throw std::invalid_argument("mat4::inverseAffine: singular linear part");
    const float inv = 1.0f / det;
    mat4 r{};
    r(0, 0) = (e * i - f * h) * inv;
    r(1, 0) = (c * h - b * i) * inv;
    r(2, 0) = (b * f - c * e) * inv;
    r(0, 1) = (f * g - d * i) * inv;
    r(1, 1) = (a * i - c * g) * inv;
    r(2, 1) = (c * d - a * f) * inv;
    r(0, 2) = (d * h - e * g) * inv;
    r(1, 2) = (b * g - a * h) * inv;
    r(2, 2) = (a * e - b * d) * inv;
    const vec3 t{m(3, 0), m(3, 1), m(3, 2)};
    const vec4 tinv = r * vec4{t.x, t.y, t.z, 0.0f};
    r(3, 0) = -tinv.x;
    r(3, 1) = -tinv.y;
    r(3, 2) = -tinv.z;
    r(3, 3) = 1.0f;
    return r;
}

}  // namespace grace::core
