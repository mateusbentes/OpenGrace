#pragma once

#include <cstddef>

#include "grace/core/math/vec3.hpp"

namespace grace::core {

/// Four-component float vector, used for homogeneous coordinates.
struct vec4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr vec4() = default;
    constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr explicit vec4(const vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr float& operator[](std::size_t index)
    {
        return index == 0 ? x : (index == 1 ? y : (index == 2 ? z : w));
    }
    constexpr float operator[](std::size_t index) const
    {
        return index == 0 ? x : (index == 1 ? y : (index == 2 ? z : w));
    }
};

constexpr vec4 operator+(const vec4& a, const vec4& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
constexpr vec4 operator-(const vec4& a, const vec4& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
constexpr vec4 operator*(const vec4& a, float s)
{
    return {a.x * s, a.y * s, a.z * s, a.w * s};
}
constexpr vec4 operator*(float s, const vec4& a) { return a * s; }

constexpr bool operator==(const vec4& a, const vec4& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
constexpr bool operator!=(const vec4& a, const vec4& b) { return !(a == b); }

constexpr float dot(const vec4& a, const vec4& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

}  // namespace grace::core
