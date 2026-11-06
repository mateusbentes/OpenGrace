#pragma once

#include <cmath>
#include <cstddef>

#include "grace/core/math/contract.hpp"
#include "grace/core/math/vec2.hpp"

namespace grace::core {

/// Three-component float vector. Scalar implementation, no intrinsics.
struct vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit vec3(const vec2& v, float z_) : x(v.x), y(v.y), z(z_) {}

    constexpr float& operator[](std::size_t index)
    {
        return index == 0 ? x : (index == 1 ? y : z);
    }
    constexpr float operator[](std::size_t index) const
    {
        return index == 0 ? x : (index == 1 ? y : z);
    }
};

constexpr vec3 operator+(const vec3& a, const vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
constexpr vec3 operator-(const vec3& a, const vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
constexpr vec3 operator-(const vec3& a) { return {-a.x, -a.y, -a.z}; }
constexpr vec3 operator*(const vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr vec3 operator*(float s, const vec3& a) { return a * s; }
constexpr vec3 operator/(const vec3& a, float s) { return {a.x / s, a.y / s, a.z / s}; }

constexpr bool operator==(const vec3& a, const vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
constexpr bool operator!=(const vec3& a, const vec3& b) { return !(a == b); }

constexpr float dot(const vec3& a, const vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr vec3 cross(const vec3& a, const vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// Euclidean length. Not constexpr in C++17 (std::sqrt is not constexpr).
inline float length(const vec3& v)
{
    return std::sqrt(dot(v, v));
}

/// Returns v scaled to unit length. A zero vector normalises to the zero
/// vector (documented Release behaviour); it never produces NaN. In Debug
/// builds a zero input is a contract violation: it is reported through
/// detail::degenerateInput (abort unless a handler is installed).
inline vec3 normalise(const vec3& v)
{
    const float len = length(v);
    if (len == 0.0f)
    {
        math::detail::degenerateInput("normalise", "zero-length vector");
        return vec3{0.0f, 0.0f, 0.0f};
    }
    return v / len;
}

}  // namespace grace::core
