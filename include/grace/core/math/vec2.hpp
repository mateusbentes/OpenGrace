#pragma once

#include <cmath>

namespace grace::core {

/// Two-component float vector. Scalar implementation, no intrinsics.
struct vec2
{
    float x = 0.0f;
    float y = 0.0f;

    constexpr vec2() = default;
    constexpr vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr float& operator[](std::size_t index) { return index == 0 ? x : y; }
    constexpr float operator[](std::size_t index) const { return index == 0 ? x : y; }
};

constexpr vec2 operator+(const vec2& a, const vec2& b) { return {a.x + b.x, a.y + b.y}; }
constexpr vec2 operator-(const vec2& a, const vec2& b) { return {a.x - b.x, a.y - b.y}; }
constexpr vec2 operator-(const vec2& a) { return {-a.x, -a.y}; }
constexpr vec2 operator*(const vec2& a, float s) { return {a.x * s, a.y * s}; }
constexpr vec2 operator*(float s, const vec2& a) { return a * s; }
constexpr vec2 operator/(const vec2& a, float s) { return {a.x / s, a.y / s}; }

constexpr bool operator==(const vec2& a, const vec2& b) { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(const vec2& a, const vec2& b) { return !(a == b); }

constexpr float dot(const vec2& a, const vec2& b) { return a.x * b.x + a.y * b.y; }

/// Euclidean length. Not constexpr in C++17 (std::sqrt is not constexpr).
inline float length(const vec2& v)
{
    return std::sqrt(dot(v, v));
}

/// Returns v scaled to unit length. A zero vector normalises to the zero
/// vector (documented behaviour); it never produces NaN.
inline vec2 normalise(const vec2& v)
{
    const float len = length(v);
    if (len == 0.0f)
        return vec2{0.0f, 0.0f};
    return v / len;
}

}  // namespace grace::core
