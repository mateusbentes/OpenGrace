#include "grace/render/paraboloid.hpp"

#include <cmath>
#include <stdexcept>

namespace grace::render {

ParaboloidCoord paraboloidProject(const core::vec3& direction)
{
    const float len = core::length(direction);
    if (len == 0.0f)
        throw std::invalid_argument("paraboloidProject: zero-length direction");
    const core::vec3 d = direction / len;
    ParaboloidCoord coord{};
    if (d.z >= 0.0f)  // seam (d.z == 0) goes to the front hemisphere
    {
        coord.hemisphere = ParaboloidHemisphere::Front;
        coord.u = d.x / (1.0f + d.z);
        coord.v = d.y / (1.0f + d.z);
    }
    else
    {
        coord.hemisphere = ParaboloidHemisphere::Back;
        coord.u = -d.x / (1.0f - d.z);
        coord.v = -d.y / (1.0f - d.z);
    }
    return coord;
}

core::vec3 paraboloidDirection(ParaboloidHemisphere hemisphere, float u, float v)
{
    const float r2 = u * u + v * v;
    if (r2 > 1.0f)
        throw std::invalid_argument("paraboloidDirection: uv outside the unit disk");
    if (hemisphere == ParaboloidHemisphere::Front)
    {
        const float z = (1.0f - r2) / (1.0f + r2);
        const float scale = 1.0f + z;
        return core::vec3{u * scale, v * scale, z};
    }
    const float z = (r2 - 1.0f) / (r2 + 1.0f);
    const float scale = 1.0f - z;
    return core::vec3{-u * scale, -v * scale, z};
}

core::mat4 paraboloidLightView(const core::vec3& lightPos, const core::vec3& forward,
                               const core::vec3& up)
{
    if (core::length(forward) == 0.0f)
        throw std::invalid_argument("paraboloidLightView: zero-length forward");
    const core::vec3 fwd = core::normalise(forward);
    const core::vec3 rightRaw = core::cross(fwd, up);
    if (core::length(rightRaw) == 0.0f)
        throw std::invalid_argument("paraboloidLightView: up parallel to forward");
    const core::vec3 right = core::normalise(rightRaw);
    const core::vec3 realUp = core::cross(right, fwd);

    // Rows are the light-space basis vectors, so light-space +Z is forward.
    core::mat4 m = core::mat4::identity();
    m(0, 0) = right.x;
    m(1, 0) = right.y;
    m(2, 0) = right.z;
    m(3, 0) = -core::dot(right, lightPos);
    m(0, 1) = realUp.x;
    m(1, 1) = realUp.y;
    m(2, 1) = realUp.z;
    m(3, 1) = -core::dot(realUp, lightPos);
    m(0, 2) = fwd.x;
    m(1, 2) = fwd.y;
    m(2, 2) = fwd.z;
    m(3, 2) = -core::dot(fwd, lightPos);
    return m;
}

}  // namespace grace::render
