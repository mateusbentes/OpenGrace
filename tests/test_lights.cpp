#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/material.hpp"
#include "grace/render/null_render_backend.hpp"
#include "grace/render/shadow.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "test_support.hpp"

namespace {

using grace::core::vec3;
using grace::render::DirectionalLight;
using grace::render::DrawSubmission;
using grace::render::DynamicLight;
using grace::render::kMaxDynamicLightsPerObject;
using grace::render::NullRenderBackend;
using grace::render::PointLight;
using grace::render::SpotLight;
using grace::render::SurfaceParams;
using grace::render::pointAttenuation;
using grace::render::spotAttenuation;
using grace::render::spotConeFactor;

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

template <typename F>
bool throwsWith(F&& fn, const std::string& needle)
{
    try
    {
        fn();
    }
    catch (const std::invalid_argument& error)
    {
        return std::string(error.what()).find(needle) != std::string::npos;
    }
    return false;
}

}  // namespace

int main()
{
    // The documented per-object limit is exactly four dynamic lights.
    GRACE_CHECK_EQ(kMaxDynamicLightsPerObject, 4u);

    // Directional light validation.
    {
        DirectionalLight sun;
        sun.validate();
        DirectionalLight bad = sun;
        bad.direction = vec3{0.0f, 0.0f, 0.0f};
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "zero-length direction"));
        bad = sun;
        bad.intensity = -1.0f;
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "intensity"));
        bad = sun;
        bad.color = vec3{-0.1f, 1.0f, 1.0f};
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "colour"));
    }

    // Point light validation: radius must be > 0.
    {
        PointLight point;
        point.validate();
        PointLight bad = point;
        bad.radius = 0.0f;
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "radius"));
        bad.radius = -5.0f;
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "radius"));
        bad = point;
        bad.intensity = -0.5f;
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "intensity"));
    }

    // Spot light validation: cone must not be inverted, angles in range,
    // direction non-zero.
    {
        SpotLight spot;
        spot.validate();
        SpotLight bad = spot;
        bad.innerAngle = 1.0f;
        bad.outerAngle = 0.5f;
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "inverted cone"));
        bad = spot;
        bad.outerAngle = 4.0f;  // > pi
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "cone angles"));
        bad = spot;
        bad.direction = vec3{0.0f, 0.0f, 0.0f};
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "zero-length direction"));
        bad = spot;
        bad.range = -1.0f;
        GRACE_CHECK(throwsWith([&] { bad.validate(); }, "range"));
    }

    // Documented attenuation model: windowed inverse square for points.
    {
        PointLight light;
        light.intensity = 10.0f;
        light.radius = 10.0f;
        GRACE_CHECK(near(pointAttenuation(light, 0.0f), 10.0f, 1e-5f));
        GRACE_CHECK(near(pointAttenuation(light, 2.0f), 10.0f * 0.64f / 5.0f, 1e-5f));
        GRACE_CHECK(near(pointAttenuation(light, 10.0f), 0.0f, 1e-6f));
        GRACE_CHECK(near(pointAttenuation(light, 20.0f), 0.0f, 1e-6f));  // beyond range
    }

    // Documented spot cone falloff: smoothstep between outer and inner.
    {
        SpotLight spot;
        spot.innerAngle = 0.5f;   // ~28.6 deg
        spot.outerAngle = 1.0f;   // ~57.3 deg
        spot.intensity = 1.0f;
        spot.range = 10.0f;
        GRACE_CHECK(near(spotConeFactor(spot, 1.0f), 1.0f, 1e-5f));   // inside inner cone
        GRACE_CHECK(near(spotConeFactor(spot, 0.0f), 0.0f, 1e-5f));   // outside outer cone
        const float cos45 = std::cos(0.7853981634f);
        const float expected_t = (cos45 - std::cos(1.0f)) / (std::cos(0.5f) - std::cos(1.0f));
        const float expected = expected_t * expected_t * (3.0f - 2.0f * expected_t);
        GRACE_CHECK(near(spotConeFactor(spot, cos45), expected, 1e-5f));  // between cones
        // Combined attenuation: cone factor times range attenuation.
        const float d = 2.0f;
        const float window = 1.0f - d / spot.range;
        const float expected_atten =
            spot.intensity * 1.0f * window * window / (d * d + 1.0f);  // cone factor 1
        GRACE_CHECK(near(spotAttenuation(spot, d, 1.0f), expected_atten, 1e-5f));
        GRACE_CHECK(near(spotAttenuation(spot, d, 0.0f), 0.0f, 1e-6f));  // outside cone
    }

    // Surface parameter validation.
    {
        SurfaceParams surface;
        grace::render::validateSurfaceParams(surface);
        SurfaceParams bad = surface;
        bad.albedo = vec3{1.5f, 1.0f, 1.0f};
        GRACE_CHECK(throwsWith([&] { grace::render::validateSurfaceParams(bad); }, "albedo"));
        bad = surface;
        bad.shininess = -1.0f;
        GRACE_CHECK(throwsWith([&] { grace::render::validateSurfaceParams(bad); }, "shininess"));
        bad = surface;
        bad.emissive = vec3{0.0f, -0.1f, 0.0f};
        GRACE_CHECK(throwsWith([&] { grace::render::validateSurfaceParams(bad); }, "emissive"));

        grace::render::Material material;
        material.setSurfaceParams(surface);
        GRACE_CHECK(throwsWith([&] { material.setSurfaceParams(bad); }, "emissive"));
    }

    // The per-object dynamic light limit is enforced at exactly the documented
    // figure; the over-limit case is rejected explicitly, never truncated.
    {
        NullRenderBackend null_backend(8, 8);
        auto mesh = null_backend.createGpuMesh();

        DrawSubmission submission;
        submission.mesh = mesh.get();
        for (std::uint32_t i = 0; i < kMaxDynamicLightsPerObject; ++i)
            submission.lights.push_back(PointLight{});
        submission.validate();  // exactly 4 is accepted

        DrawSubmission over = submission;
        over.lights.push_back(SpotLight{});
        GRACE_CHECK(throwsWith([&] { over.validate(); }, "exceeds the per-object limit of 4"));

        DrawSubmission no_mesh;
        GRACE_CHECK(throwsWith([&] { no_mesh.validate(); }, "null mesh"));

        // The null backend accepts and records the same submissions.
        null_backend.submit(submission);
        GRACE_CHECK(!null_backend.events().empty());
        GRACE_CHECK(null_backend.events().back() == std::string("submit(lights=4,sun=0,shadow=0)"));
        GRACE_CHECK_EQ(null_backend.violationCount(), 0u);
        null_backend.shutdown();
        null_backend.submit(submission);  // after shutdown: recorded + violation
        GRACE_CHECK_EQ(null_backend.violationCount(), 1u);
    }

    // Orthographic sun light-space matrix: the bounds center maps to the
    // centre of the shadow map, and invalid inputs are rejected.
    {
        using grace::render::SceneBounds;
        using grace::render::sunLightSpaceMatrix;
        DirectionalLight sun;
        sun.direction = vec3{0.0f, 0.0f, -1.0f};
        SceneBounds bounds;
        bounds.center = vec3{1.0f, 2.0f, 3.0f};
        bounds.radius = 2.0f;
        const grace::core::mat4 m = sunLightSpaceMatrix(sun, bounds, 0.1f, 4.1f);
        const grace::core::vec4 clip =
            m * grace::core::vec4{bounds.center.x, bounds.center.y, bounds.center.z, 1.0f};
        GRACE_CHECK(near(clip.x / clip.w, 0.0f, 1e-4f));
        GRACE_CHECK(near(clip.y / clip.w, 0.0f, 1e-4f));
        GRACE_CHECK(clip.z / clip.w > -1.0f && clip.z / clip.w < 1.0f);

        bool threw = false;
        try
        {
            static_cast<void>(sunLightSpaceMatrix(sun, bounds, 4.1f, 0.1f));  // far <= near
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
        SceneBounds bad_bounds;
        bad_bounds.radius = 0.0f;
        threw = false;
        try
        {
            static_cast<void>(sunLightSpaceMatrix(sun, bad_bounds, 0.1f, 4.1f));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
        DirectionalLight bad_sun;
        bad_sun.direction = vec3{0.0f, 0.0f, 0.0f};
        threw = false;
        try
        {
            static_cast<void>(sunLightSpaceMatrix(bad_sun, bounds, 0.1f, 4.1f));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
    }

    return grace::test::summary("lights");
}
