#include "grace/render/paraboloid.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "test_support.hpp"

namespace {

using grace::core::vec3;
using grace::render::ParaboloidCoord;
using grace::render::ParaboloidHemisphere;
using grace::render::paraboloidDirection;
using grace::render::paraboloidLightView;
using grace::render::paraboloidProject;

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

bool nearVec(const vec3& a, const vec3& b, float eps)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

template <typename F>
bool throwsInvalid(F&& fn)
{
    try
    {
        fn();
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    // Round trip: direction -> uv -> direction, both hemispheres, within the
    // documented tolerance.
    {
        const vec3 directions[] = {
            vec3{0.0f, 0.0f, 1.0f},    // front pole
            vec3{0.6f, 0.0f, 0.8f},    // front
            vec3{-0.3f, 0.5f, 0.81f},  // front
            vec3{0.0f, 0.0f, -1.0f},   // back pole (singularity behind the light)
            vec3{0.6f, 0.0f, -0.8f},   // back
            vec3{-0.5f, -0.5f, -0.7f}, // back
        };
        for (const vec3& dir : directions)
        {
            const vec3 unit = grace::core::normalise(dir);
            const ParaboloidCoord coord = paraboloidProject(unit);
            // uv stays inside the unit disk.
            GRACE_CHECK(coord.u * coord.u + coord.v * coord.v <= 1.0f + 1e-5f);
            const vec3 back = paraboloidDirection(coord.hemisphere, coord.u, coord.v);
            GRACE_CHECK(nearVec(back, unit, 1e-5f));
        }
    }

    // Hemisphere convention: front is d.z >= 0, seam (d.z == 0) goes front.
    {
        const ParaboloidCoord front = paraboloidProject(vec3{0.6f, 0.0f, 0.8f});
        GRACE_CHECK(front.hemisphere == ParaboloidHemisphere::Front);
        const ParaboloidCoord back = paraboloidProject(vec3{0.6f, 0.0f, -0.8f});
        GRACE_CHECK(back.hemisphere == ParaboloidHemisphere::Back);
        const ParaboloidCoord seam = paraboloidProject(vec3{1.0f, 0.0f, 0.0f});
        GRACE_CHECK(seam.hemisphere == ParaboloidHemisphere::Front);
        GRACE_CHECK(near(seam.u, 1.0f, 1e-6f));
        // At the seam both hemispheres give d.z = 0.
        const vec3 seamBack = paraboloidDirection(ParaboloidHemisphere::Back, -1.0f, 0.0f);
        GRACE_CHECK(near(seamBack.z, 0.0f, 1e-6f));
        // The singularity behind the light is the back pole: valid in the back
        // map (uv 0,0), and it maps to -Z.
        const vec3 pole = paraboloidDirection(ParaboloidHemisphere::Back, 0.0f, 0.0f);
        GRACE_CHECK(nearVec(pole, vec3{0.0f, 0.0f, -1.0f}, 1e-6f));
        const ParaboloidCoord poleCoord = paraboloidProject(vec3{0.0f, 0.0f, -1.0f});
        GRACE_CHECK(poleCoord.hemisphere == ParaboloidHemisphere::Back);
        GRACE_CHECK(near(poleCoord.u, 0.0f, 1e-6f) && near(poleCoord.v, 0.0f, 1e-6f));
        // Degenerate inputs rejected.
        GRACE_CHECK(throwsInvalid([] { static_cast<void>(paraboloidProject(vec3{0, 0, 0})); }));
        GRACE_CHECK(throwsInvalid([] {
            static_cast<void>(paraboloidDirection(ParaboloidHemisphere::Front, 2.0f, 0.0f));
        }));
    }

    // Light view matrix: light at origin looking +Z maps +Z to +Z light space.
    {
        const grace::core::mat4 m =
            paraboloidLightView(vec3{1.0f, 2.0f, 3.0f}, vec3{0.0f, 0.0f, 1.0f},
                                vec3{0.0f, 1.0f, 0.0f});
        const grace::core::vec4 light = m * grace::core::vec4{1.0f, 2.0f, 4.0f, 1.0f};
        GRACE_CHECK(near(light.x, 0.0f, 1e-5f));
        GRACE_CHECK(near(light.y, 0.0f, 1e-5f));
        GRACE_CHECK(near(light.z, 1.0f, 1e-5f));  // one unit ahead of the light
        GRACE_CHECK(throwsInvalid([&] {
            static_cast<void>(paraboloidLightView(vec3{0, 0, 0}, vec3{0, 0, 0}, vec3{0, 1, 0}));
        }));
        GRACE_CHECK(throwsInvalid([&] {
            static_cast<void>(paraboloidLightView(vec3{0, 0, 0}, vec3{0, 1, 0}, vec3{0, 1, 0}));
        }));
    }

    return grace::test::summary("paraboloid");
}
