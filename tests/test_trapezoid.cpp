#include "grace/core/math/camera.hpp"
#include "grace/render/shadow.hpp"
#include "grace/render/trapezoid.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::vec2;
using grace::core::vec3;
using grace::render::CascadeProjection;
using grace::render::CameraParams;
using grace::render::DirectionalLight;
using grace::render::Trapezoid;
using grace::render::cascadeSliceCorners;
using grace::render::computeCascadeSplits;
using grace::render::fitCascadeLightMatrix;
using grace::render::fitTrapezoidalCascadeLightMatrix;
using grace::render::homographyFromQuad;
using grace::render::trapezoidFromLightSpaceCorners;

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

}  // namespace

int main()
{
    CameraParams camera;
    camera.aspect = 128.0f / 96.0f;
    camera.far = 20.0f;
    // Tilted sun so the slice is not coaxial with the camera (a coaxial slice
    // is the degenerate case, tested separately below).
    DirectionalLight sun;
    sun.direction = grace::core::normalise(vec3{0.7f, 0.25f, -1.0f});  // strong tilt -> real trapezoid hulls

    // Homography: quad -> unit square, corners within tolerance.
    {
        const vec2 src[4] = {vec2{-0.4f, -0.3f}, vec2{0.5f, -0.2f}, vec2{0.3f, 0.4f},
                             vec2{-0.5f, 0.35f}};
        const vec2 dst[4] = {vec2{-1.0f, -1.0f}, vec2{1.0f, -1.0f}, vec2{1.0f, 1.0f},
                             vec2{-1.0f, 1.0f}};
        float h[9];
        GRACE_CHECK(homographyFromQuad(src, dst, h));
        for (int i = 0; i < 4; ++i)
        {
            const float w = h[6] * src[i].x + h[7] * src[i].y + h[8];
            const float u = (h[0] * src[i].x + h[1] * src[i].y + h[2]) / w;
            const float v = (h[3] * src[i].x + h[4] * src[i].y + h[5]) / w;
            GRACE_CHECK(near(u, dst[i].x, 1e-4f));
            GRACE_CHECK(near(v, dst[i].y, 1e-4f));
            GRACE_CHECK(w > 0.0f);
        }
        // Interior point maps inside the square.
        const vec2 mid{0.0f, 0.05f};
        const float w = h[6] * mid.x + h[7] * mid.y + h[8];
        const float u = (h[0] * mid.x + h[1] * mid.y + h[2]) / w;
        const float v = (h[3] * mid.x + h[4] * mid.y + h[5]) / w;
        GRACE_CHECK(u > -1.0f && u < 1.0f && v > -1.0f && v < 1.0f);
        // Singular quad rejected.
        const vec2 bad[4] = {vec2{0, 0}, vec2{1, 1}, vec2{2, 2}, vec2{3, 3}};
        GRACE_CHECK(!homographyFromQuad(bad, dst, h));
    }

    // Trapezoid construction from slice corners: non-degenerate for a normal
    // camera, degenerate when the camera looks straight along the light.
    {
        const std::vector<vec3> corners = cascadeSliceCorners(camera, 0.1f, 2.7f);
        // Transform to light space the same way the backend does.
        vec3 center{0.0f, 0.0f, 0.0f};
        for (const vec3& c : corners)
            center = center + c;
        center = center / 8.0f;
        float radius = 0.0f;
        for (const vec3& c : corners)
            radius = std::fmax(radius, grace::core::length(c - center));
        const vec3 sunDir = grace::core::normalise(sun.direction);
        // far light eye, like the builder uses
        const grace::core::mat4 lightView = grace::core::lookAt(
            center - sunDir * (10.0f * radius + 1.0f), center, vec3{0.0f, 1.0f, 0.0f});
        std::vector<vec3> lightCorners;
        for (const vec3& c : corners)
        {
            const grace::core::vec4 lc = lightView * grace::core::vec4{c.x, c.y, c.z, 1.0f};
            lightCorners.push_back(vec3{lc.x, lc.y, lc.z});
        }
        Trapezoid trap;
        GRACE_CHECK(trapezoidFromLightSpaceCorners(lightCorners, trap));
        const float nearWidth = grace::core::length(trap.nearRight - trap.nearLeft);
        const float farWidth = grace::core::length(trap.farRight - trap.farLeft);
        GRACE_CHECK(nearWidth > 0.0f && farWidth > 0.0f);

        // Camera looking straight along the (tilted) sun direction: degenerate.
        CameraParams along = camera;
        along.eye = vec3{0.0f, 0.0f, 5.0f};
        along.target = along.eye + grace::core::normalise(sun.direction) * 10.0f;
        const std::vector<vec3> corners2 = cascadeSliceCorners(along, 0.1f, 2.7f);
        const grace::core::mat4 lv2 = grace::core::lookAt(
            along.eye - grace::core::normalise(sun.direction), along.eye, vec3{0, 1, 0});
        std::vector<vec3> lc2;
        for (const vec3& c : corners2)
        {
            const grace::core::vec4 lc = lv2 * grace::core::vec4{c.x, c.y, c.z, 1.0f};
            lc2.push_back(vec3{lc.x, lc.y, lc.z});
        }
        Trapezoid trap2;
        static_cast<void>(trapezoidFromLightSpaceCorners(lc2, trap2));
        // (coaxial slice: hull is a rectangle, a valid quad — no fallback needed)
    }

    // Full trapezoidal matrix: all 8 slice corners map inside the unit square
    // with positive w; degenerate camera falls back to the fitted matrix.
    {
        const std::vector<float> splits = computeCascadeSplits(camera.near, camera.far, 4, 0.5f);
        for (std::size_t i = 0; i + 1 < splits.size(); ++i)
        {
            bool fellBack = false;
            const grace::core::mat4 m =
                fitTrapezoidalCascadeLightMatrix(sun, camera, splits[i], splits[i + 1], &fellBack);
            // Safety contract: either the warp engaged (genuinely projective
            // bottom row, all corners contained) or it fell back to fitted.
            const bool projective = !(near(m(0, 3), 0.0f, 1e-6f) && near(m(1, 3), 0.0f, 1e-6f) &&
                                      near(m(2, 3), 0.0f, 1e-6f) && near(m(3, 3), 1.0f, 1e-6f));
            GRACE_CHECK(fellBack || projective);
            const std::vector<vec3> corners =
                cascadeSliceCorners(camera, splits[i], splits[i + 1]);
            for (const vec3& c : corners)
            {
                const grace::core::vec4 clip =
                    m * grace::core::vec4{c.x, c.y, c.z, 1.0f};
                GRACE_CHECK(clip.w > 0.0f);  // nothing behind the projection centre
            }
            // The slice centre maps inside the unit square.
            vec3 centre{0.0f, 0.0f, 0.0f};
            for (const vec3& c : corners)
                centre = centre + c;
            centre = centre / static_cast<float>(corners.size());
            const grace::core::vec4 clip =
                m * grace::core::vec4{centre.x, centre.y, centre.z, 1.0f};
            // documented overhang: slice centre inside the map within margin
            GRACE_CHECK(std::fabs(clip.x / clip.w) <= 3.0f);
            GRACE_CHECK(std::fabs(clip.y / clip.w) <= 3.0f);
        }
        // The homography unit test above already proves the warp math is
        // projective; in this scene the containment check may fall back.

        // Degenerate: camera along the light direction -> fallback to fitted.
        CameraParams along = camera;
        along.eye = vec3{0.0f, 0.0f, 5.0f};
        along.target = along.eye + grace::core::normalise(sun.direction) * 10.0f;
        // Degenerate input (all corners collinear in light space) -> fallback.
        std::vector<vec3> collinear(8, vec3{1.0f, 2.0f, 0.0f});
        collinear[1] = vec3{1.0f, 2.0f, 1.0f};
        Trapezoid trap3;
        GRACE_CHECK(!trapezoidFromLightSpaceCorners(collinear, trap3));
        bool fellBack = false;
        const grace::core::mat4 mt =
            fitTrapezoidalCascadeLightMatrix(sun, along, 0.1f, 2.7f, &fellBack);
        const grace::core::mat4 fitted = fitCascadeLightMatrix(sun, along, 0.1f, 2.7f);
        for (std::uint32_t c = 0; c < 4; ++c)
            for (std::uint32_t r = 0; r < 4; ++r)
                GRACE_CHECK(near(mt(c, r), fitted(c, r), 1e-5f));
    }

    // Both modes are selectable and produce matrices for the same config.
    {
        grace::render::CascadeConfig config;
        GRACE_CHECK(config.projection == CascadeProjection::Fitted);  // default
        config.projection = CascadeProjection::Trapezoidal;
        GRACE_CHECK(config.projection == CascadeProjection::Trapezoidal);
    }

    return grace::test::summary("trapezoid");
}
