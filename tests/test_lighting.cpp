#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/opengl_render_backend.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::vec3;
using grace::render::ClearColor;
using grace::render::ComponentType;
using grace::render::DirectionalLight;
using grace::render::DrawSubmission;
using grace::render::DynamicLight;
using grace::render::Material;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::PointLight;
using grace::render::SpotLight;
using grace::render::SurfaceParams;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;
using grace::render::pointAttenuation;
using grace::render::spotAttenuation;

bool withinTol(ClearColor actual, ClearColor expected, int tol)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= tol && diff(actual.g, expected.g) <= tol &&
           diff(actual.b, expected.b) <= tol && diff(actual.a, expected.a) <= tol;
}

ClearColor toColor(float r, float g, float b)
{
    const auto to8 = [](float v) {
        const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
    };
    return ClearColor{to8(r), to8(g), to8(b), 255};
}

// Flat triangle in the z=0 plane facing +Z (towards the diagnostic camera).
Mesh makeLitTriangle()
{
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 6 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Normal, ComponentType::Float32, 3, 3 * sizeof(float), false});
    mesh.setLayout(layout);
    const vec3 normal{0.0f, 0.0f, 1.0f};
    mesh.setVertexData(std::vector<float>{
        -0.5f, -0.5f, 0.0f, normal.x, normal.y, normal.z,  //
        0.5f, -0.5f, 0.0f, normal.x, normal.y, normal.z,   //
        0.0f, 0.5f, 0.0f, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2});
    return mesh;
}

}  // namespace

int main()
{
    OpenGLBackendConfig config;
    config.width = 128;
    config.height = 96;  // non-square on purpose
    config.hidden = true;
    config.readback_center = true;

    std::unique_ptr<OpenGLRenderBackend> backend;
    try
    {
        backend = std::make_unique<OpenGLRenderBackend>(config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        std::printf("SKIP: no GL context in this environment: %s\n", error.what());
        return 77;
    }

    auto gpu_mesh = backend->createGpuMesh();
    gpu_mesh->upload(makeLitTriangle());

    const ClearColor black{0, 0, 0, 255};
    backend->setClearColor(black);

    SurfaceParams surface;  // albedo white, no specular, no emissive
    surface.albedo = vec3{1.0f, 1.0f, 1.0f};
    surface.specular = vec3{0.0f, 0.0f, 0.0f};

    // 1. A surface lit by a directional light alone: Lambert term only.
    {
        DrawSubmission submission;
        submission.mesh = gpu_mesh.get();
        submission.material.setSurfaceParams(surface);
        DirectionalLight sun;
        sun.direction = vec3{0.0f, 0.0f, -1.0f};  // shining onto the +Z-facing surface
        sun.color = vec3{0.5f, 0.5f, 0.5f};
        sun.intensity = 1.0f;
        submission.sun = sun;
        backend->clear();
        backend->submit(submission);
        backend->readback();
        // N = (0,0,1), L = (0,0,1) -> ndl = 1; result = albedo * sunColor * 1 = 0.5.
        const ClearColor expected = toColor(0.5f, 0.5f, 0.5f);
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // 2. A point light at a known distance: documented falloff, within tolerance.
    {
        DrawSubmission submission;
        submission.mesh = gpu_mesh.get();
        submission.material.setSurfaceParams(surface);
        PointLight point;
        point.position = vec3{0.0f, 0.0f, 2.0f};  // distance 2 from the surface at the centre
        point.color = vec3{1.0f, 1.0f, 1.0f};
        point.intensity = 1.0f;
        point.radius = 10.0f;
        submission.lights.push_back(point);
        backend->clear();
        backend->submit(submission);
        backend->readback();
        const float atten = pointAttenuation(point, 2.0f);  // ndl = 1 at the centre
        const ClearColor expected = toColor(atten, atten, atten);
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // 3. A spot light inside its cone vs outside its cone: different, expected
    //    pixels.
    {
        SpotLight spot;
        spot.position = vec3{0.0f, 0.0f, 2.0f};
        spot.direction = vec3{0.0f, 0.0f, -1.0f};  // straight at the surface: inside the cone
        spot.color = vec3{1.0f, 1.0f, 1.0f};
        spot.intensity = 1.0f;
        spot.range = 10.0f;
        spot.innerAngle = 0.5f;
        spot.outerAngle = 1.0f;

        DrawSubmission inside;
        inside.mesh = gpu_mesh.get();
        inside.material.setSurfaceParams(surface);
        inside.lights.push_back(spot);
        backend->clear();
        backend->submit(inside);
        backend->readback();
        const float atten_in = spotAttenuation(spot, 2.0f, 1.0f);  // cosAngle = 1
        const ClearColor expected_in = toColor(atten_in, atten_in, atten_in);
        GRACE_CHECK(withinTol(backend->centerPixel(), expected_in, 2));

        SpotLight outside_light = spot;
        outside_light.direction = vec3{1.0f, 0.0f, 0.0f};  // 90 deg off: outside the outer cone
        DrawSubmission outside;
        outside.mesh = gpu_mesh.get();
        outside.material.setSurfaceParams(surface);
        outside.lights.push_back(outside_light);
        backend->clear();
        backend->submit(outside);
        backend->readback();
        // Cone factor 0 -> only the emissive term, which is zero here.
        const ClearColor expected_out = toColor(0.0f, 0.0f, 0.0f);
        GRACE_CHECK(withinTol(backend->centerPixel(), expected_out, 2));
        GRACE_CHECK(!withinTol(backend->centerPixel(), expected_in, 2));  // genuinely different
    }

    // 4. No light at all: only the emissive term appears.
    {
        SurfaceParams emissive_surface = surface;
        emissive_surface.emissive = vec3{0.25f, 0.5f, 0.75f};
        DrawSubmission submission;
        submission.mesh = gpu_mesh.get();
        submission.material.setSurfaceParams(emissive_surface);
        backend->clear();
        backend->submit(submission);
        backend->readback();
        const ClearColor expected = toColor(0.25f, 0.5f, 0.75f);
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // 5. The over-limit submission is rejected by the backend with the
    //    documented message (never silently truncated).
    {
        DrawSubmission over;
        over.mesh = gpu_mesh.get();
        over.material.setSurfaceParams(surface);
        for (int i = 0; i < 5; ++i)
            over.lights.push_back(PointLight{});
        bool threw = false;
        try
        {
            backend->submit(over);
        }
        catch (const std::invalid_argument& error)
        {
            threw = std::string(error.what()).find("exceeds the limit of 4") !=
                    std::string::npos;
        }
        GRACE_CHECK(threw);
        // Exactly four is accepted.
        DrawSubmission four = over;
        four.lights.pop_back();
        backend->clear();
        backend->submit(four);
        GRACE_CHECK_EQ(backend->violationCount(), 0u);
    }

    // 6b (A3): the ambient term contributes the expected value to an unlit
    // surface. submission.ambient is the scene term (the renderer fills it
    // from RendererConfig::ambient); here it is set directly.
    {
        DrawSubmission unlit;
        unlit.mesh = gpu_mesh.get();
        unlit.material.setSurfaceParams(surface);  // albedo white
        unlit.ambient = 0.1f;
        backend->clear();
        backend->submit(unlit);
        backend->readback();
        const ClearColor p = backend->centerPixel();
        // expected: ambient * albedo = 0.1 linear -> 0.1 * 255 = 25.5 -> 26
        std::printf("ambient on unlit surface: pixel=%d,%d,%d,%d (expected ~26)\n",
                    static_cast<int>(p.r), static_cast<int>(p.g), static_cast<int>(p.b),
                    static_cast<int>(p.a));
        GRACE_CHECK(withinTol(p, ClearColor{26, 26, 26, 255}, 2));
    }

    GRACE_CHECK_EQ(backend->violationCount(), 0u);
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("lighting");
}
