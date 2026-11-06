#include "grace/render/camera.hpp"
#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/shadow.hpp"

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
using grace::render::DrawSubmission;
using grace::render::DynamicLight;
using grace::render::kMaxShadowCastingPointLights;
using grace::render::Material;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::PointLight;
using grace::render::PointShadowConfig;
using grace::render::PointShadowMaps;
using grace::render::RenderTarget;
using grace::render::SurfaceParams;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;
using grace::render::pointAttenuation;
using grace::render::renderPointShadowMaps;

bool withinTol(ClearColor actual, ClearColor expected, int tol)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= tol && diff(actual.g, expected.g) <= tol &&
           diff(actual.b, expected.b) <= tol && diff(actual.a, expected.a) <= tol;
}

ClearColor toColor(float v)
{
    const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    const auto c = static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
    return ClearColor{c, c, c, 255};
}

// Flat triangle at z=0 with the given normal (facing +Z or -Z).
Mesh makeMesh(const vec3& normal)
{
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 6 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    layout.attributes.push_back(VertexAttribute{VertexSemantic::Normal, ComponentType::Float32,
                                                3, 3 * sizeof(float), false});
    mesh.setLayout(layout);
    mesh.setVertexData(std::vector<float>{
        -0.5f, -0.5f, 0.0f, normal.x, normal.y, normal.z,  //
        0.5f, -0.5f, 0.0f, normal.x, normal.y, normal.z,   //
        0.0f, 0.5f, 0.0f, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2});
    return mesh;
}

void printPixel(const char* label, ClearColor pixel)
{
    std::printf("point shadow check %s: pixel=%d,%d,%d,%d\n", label, static_cast<int>(pixel.r),
                static_cast<int>(pixel.g), static_cast<int>(pixel.b), static_cast<int>(pixel.a));
}

}  // namespace

int main()
{
    OpenGLBackendConfig config;
    config.width = 128;
    config.height = 96;
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

    auto groundMesh = backend->createGpuMesh();
    groundMesh->upload(makeMesh(vec3{0.0f, 0.0f, 1.0f}));   // z=0, faces +Z
    auto ceilingMesh = backend->createGpuMesh();
    ceilingMesh->upload(makeMesh(vec3{0.0f, 0.0f, -1.0f}));  // faces -Z

    const ClearColor black{0, 0, 0, 255};
    backend->setClearColor(black);

    SurfaceParams surface;
    surface.albedo = vec3{1.0f, 1.0f, 1.0f};

    // One point light between the camera (z=1) and the ground (z=0), at z=0.5.
    // The ground sees it in the BACK hemisphere (direction light->ground is -Z);
    // a ceiling at z=0.8 (still in front of the camera) sees it in the FRONT
    // hemisphere (+Z).
    PointLight light;
    light.position = vec3{0.0f, 0.0f, 0.5f};
    light.color = vec3{1.0f, 1.0f, 1.0f};
    light.intensity = 0.7f;
    light.radius = 10.0f;

    DrawSubmission ground;  // z = 0
    ground.mesh = groundMesh.get();
    ground.material.setSurfaceParams(surface);
    ground.lights.push_back(DynamicLight{light});

    DrawSubmission occluderBack = ground;  // between the light (z=0.5) and the ground (z=0.25)
    occluderBack.model = grace::core::translation(vec3{0.0f, 0.0f, 0.25f});

    DrawSubmission ceiling = ground;  // z = 0.8, facing down towards the light
    ceiling.mesh = ceilingMesh.get();
    ceiling.model = grace::core::translation(vec3{0.0f, 0.0f, 0.8f});

    DrawSubmission occluderFront = ceiling;  // between the light and the ceiling (z=0.65)
    occluderFront.model = grace::core::translation(vec3{0.0f, 0.0f, 0.65f});

    PointShadowConfig pointConfig;
    pointConfig.mapSize = 256;
    pointConfig.near = 0.1f;
    pointConfig.far = 10.0f;

    std::vector<RenderTarget*> allTargets;
    auto renderMaps = [&](const std::vector<DrawSubmission>& casters) {
        PointShadowMaps maps = renderPointShadowMaps(
            *backend, {DynamicLight{light}}, casters, pointConfig);
        for (const auto& entry : maps.entries)
        {
            allTargets.push_back(entry.front);
            allTargets.push_back(entry.back);
        }
        return maps;
    };

    // Case 1: BACK hemisphere — ground lit by the point light, no occluder.
    {
        PointShadowMaps maps = renderMaps({ground});
        DrawSubmission sub = ground;
        sub.pointShadows = &maps;
        sub.shadowLightRequests = 1;
        backend->clear();
        backend->submit(sub);
        backend->readback();
        const float atten = pointAttenuation(light, 0.5f);  // ground centre is 0.5 units away
        const ClearColor expected = toColor(atten);
        printPixel("back lit", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Case 2: BACK hemisphere — occluder between light and ground -> shadowed.
    {
        PointShadowMaps maps = renderMaps({ground, occluderBack});
        DrawSubmission sub = ground;
        sub.pointShadows = &maps;
        sub.shadowLightRequests = 1;
        backend->clear();
        backend->submit(sub);
        backend->readback();
        const ClearColor expected = toColor(0.0f);
        printPixel("back occluded", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Case 3: FRONT hemisphere — ceiling (z=3) lit, then occluded.
    {
        PointShadowMaps maps = renderMaps({ceiling});
        DrawSubmission sub = ceiling;
        sub.lights.clear();
        sub.lights.push_back(DynamicLight{light});
        sub.pointShadows = &maps;
        sub.shadowLightRequests = 1;
        backend->clear();
        backend->submit(sub);
        backend->readback();
        const float atten = pointAttenuation(light, 0.3f);  // ceiling centre is 0.3 units away
        const ClearColor expected = toColor(atten);
        printPixel("front lit", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));

        PointShadowMaps mapsOcc = renderMaps({ceiling, occluderFront});
        DrawSubmission subOcc = sub;
        subOcc.pointShadows = &mapsOcc;
        backend->clear();
        backend->submit(subOcc);
        backend->readback();
        const ClearColor expectedOcc = toColor(0.0f);
        printPixel("front occluded", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expectedOcc, 2));
    }

    // Case 4: budget — a request beyond kMaxShadowCastingPointLights is counted
    // as a violation; the excess lights are lit without a shadow map.
    {
        GRACE_CHECK_EQ(kMaxShadowCastingPointLights, 2u);
        const std::uint64_t before = backend->violationCount();
        DrawSubmission sub = ground;
        sub.pointShadows = nullptr;
        sub.shadowLightRequests = kMaxShadowCastingPointLights + 1u;
        backend->clear();
        backend->submit(sub);
        GRACE_CHECK_EQ(backend->violationCount(), before + 1u);
    }

    for (RenderTarget* t : allTargets)
        backend->releaseRenderTarget(t);

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("point_shadows");
}
