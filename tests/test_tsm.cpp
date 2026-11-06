#include "grace/render/camera.hpp"
#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/shadow.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::vec3;
using grace::render::CascadeConfig;
using grace::render::CascadeProjection;
using grace::render::ClearColor;
using grace::render::ComponentType;
using grace::render::DirectionalLight;
using grace::render::DrawSubmission;
using grace::render::Material;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::RenderTarget;
using grace::render::ShadowMap;
using grace::render::SurfaceParams;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;
using grace::render::computeCascadeSplits;
using grace::render::renderShadowCascades;

bool withinTol(ClearColor actual, ClearColor expected, int tol)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= tol && diff(actual.g, expected.g) <= tol &&
           diff(actual.b, expected.b) <= tol && diff(actual.a, expected.a) <= tol;
}

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

// World-space extent covered by one shadow-map texel along +x on the plane
// z = planeZ, measured by forward-projecting through the cascade matrix.
float worldPerTexel(const grace::core::mat4& lightMatrix, float planeZ, float mapSize)
{
    const auto uvX = [&](float x) {
        const grace::core::vec4 clip =
            lightMatrix * grace::core::vec4{x, 0.0f, planeZ, 1.0f};
        return clip.x / clip.w;
    };
    const float dx = 0.001f;
    const float duv = std::fabs(uvX(dx) - uvX(0.0f)) / dx;  // uv units per world unit
    const float texelUv = 2.0f / static_cast<float>(mapSize);
    return texelUv / duv;  // world units per texel
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

    grace::render::CameraParams camera = backend->camera();
    camera.far = 20.0f;
    backend->setCamera(camera);

    auto groundMesh = backend->createGpuMesh();
    groundMesh->upload(makeMesh(vec3{0.0f, 0.0f, 1.0f}));

    const ClearColor black{0, 0, 0, 255};
    backend->setClearColor(black);

    SurfaceParams surface;
    surface.albedo = vec3{1.0f, 1.0f, 1.0f};

    DirectionalLight sun;
    sun.direction = grace::core::normalise(vec3{0.55f, 0.2f, -1.0f});  // tilted: real parallax, trapezoid engages
    sun.color = vec3{0.5f, 0.5f, 0.5f};
    sun.intensity = 1.0f;
    const vec3 sunDir = grace::core::normalise(sun.direction);
    const float ndl = -sunDir.z;  // ground normal (0,0,1): dot(N, -sunDir)
    const ClearColor litExpected = ClearColor{
        static_cast<std::uint8_t>(0.5f * ndl * 255.0f + 0.5f),
        static_cast<std::uint8_t>(0.5f * ndl * 255.0f + 0.5f),
        static_cast<std::uint8_t>(0.5f * ndl * 255.0f + 0.5f), 255};

    DrawSubmission ground;
    ground.mesh = groundMesh.get();
    ground.material.setSurfaceParams(surface);
    ground.sun = sun;

    DrawSubmission occNear = ground;  // between the sun and the ground centre, along the light
    occNear.model = grace::core::translation(-sunDir * 0.5f);

    DrawSubmission groundFar = ground;
    groundFar.model = grace::core::translation(vec3{0.0f, 0.0f, -8.0f});
    DrawSubmission occFar = groundFar;  // between the sun and the far surface centre
    occFar.model = grace::core::translation(vec3{0.0f, 0.0f, -8.0f}) *
                   grace::core::translation(-sunDir * 0.5f);

    const char* modeName[2] = {"fitted", "trapezoidal"};
    std::vector<RenderTarget*> targets;
    for (int mode = 0; mode < 2; ++mode)
    {
        const CascadeProjection projMode = mode == 0 ? CascadeProjection::Fitted
                                                      : CascadeProjection::Trapezoidal;
        CascadeConfig cc;
        cc.count = 4;
        cc.mapSize = 256;
        cc.projection = projMode;

        auto renderMaps = [&](const std::vector<DrawSubmission>& casters) {
            ShadowMap map = renderShadowCascades(*backend, sun, backend->camera(), casters, cc);
            for (RenderTarget* t : map.targets)
                targets.push_back(t);
            return map;
        };
        auto releaseMaps = [&](const ShadowMap& map) {
            for (RenderTarget* t : map.targets)
                backend->releaseRenderTarget(t);
        };

        std::printf("=== mode: %s ===\n", modeName[mode]);

        // E3: self-shadow acne test — sun-lit ground, no occluder. Expected lit
        // value is 0.5 -> 128; a lower pixel means the constant bias fails here.
        {
            ShadowMap maps = renderMaps({ground});
            DrawSubmission sub = ground;
            sub.shadow = &maps;
            backend->clear();
            backend->submit(sub);
            backend->readback();
            const ClearColor p = backend->centerPixel();
            std::printf("%s acne/self-shadow: centre pixel = %d,%d,%d,%d (lit expected ~128)\n",
                        modeName[mode], static_cast<int>(p.r), static_cast<int>(p.g), static_cast<int>(p.b), static_cast<int>(p.a));
            GRACE_CHECK(withinTol(p, litExpected, 2));
            releaseMaps(maps);
        }

        // Near occluder shadows the near surface.
        {
            ShadowMap maps = renderMaps({ground, occNear});
            DrawSubmission sub = ground;
            sub.shadow = &maps;
            backend->clear();
            backend->submit(sub);
            backend->readback();
            const ClearColor p = backend->centerPixel();
            std::printf("%s near occluder: centre pixel = %d,%d,%d,%d (shadowed expected ~0)\n",
                        modeName[mode], static_cast<int>(p.r), static_cast<int>(p.g), static_cast<int>(p.b), static_cast<int>(p.a));
            GRACE_CHECK(withinTol(p, ClearColor{0, 0, 0, 255}, 2));
            releaseMaps(maps);
        }

        // Far occluder shadows the far surface (a different cascade).
        {
            ShadowMap maps = renderMaps({groundFar, occFar});
            DrawSubmission sub = groundFar;
            sub.shadow = &maps;
            backend->clear();
            backend->submit(sub);
            backend->readback();
            const ClearColor p = backend->centerPixel();
            std::printf("%s far occluder: centre pixel = %d,%d,%d,%d (shadowed expected ~0)\n",
                        modeName[mode], static_cast<int>(p.r), static_cast<int>(p.g), static_cast<int>(p.b), static_cast<int>(p.a));
            GRACE_CHECK(withinTol(p, ClearColor{0, 0, 0, 255}, 2));
            releaseMaps(maps);
        }

        // E4: texel density — world units per texel near the camera (view depth 1,
        // ground plane z=0) and far (view depth 15, plane z=-14).
        {
            ShadowMap maps = renderMaps({ground});
            const std::vector<float> splits =
                computeCascadeSplits(camera.near, camera.far, cc.count, cc.splitLambda);
            auto cascadeFor = [&](float depth) {
                for (std::uint32_t i = 0; i + 1 < splits.size(); ++i)
                    if (depth >= splits[i] && depth < splits[i + 1])
                        return static_cast<int>(i);
                return static_cast<int>(splits.size()) - 2;
            };
            const int nearCascade = cascadeFor(1.0f);
            const int farCascade = cascadeFor(15.0f);
            const float nearDensity = worldPerTexel(maps.lightMatrices[static_cast<std::size_t>(nearCascade)], 0.0f, static_cast<float>(cc.mapSize));
            const float farDensity = worldPerTexel(maps.lightMatrices[static_cast<std::size_t>(farCascade)], -14.0f, static_cast<float>(cc.mapSize));
            std::printf("%s texel density: near (depth 1) = %.4f world/texel, far (depth 15) = %.4f world/texel, ratio far/near = %.2f\n",
                        modeName[mode], nearDensity, farDensity, farDensity / nearDensity);
            GRACE_CHECK(nearDensity > 0.0f);
            GRACE_CHECK(farDensity > 0.0f);
            releaseMaps(maps);
        }

        // E4: temporal stability — same scene, camera displaced 0.02 in x; the
        // trapezoid depends on the camera, the fitted box (axis-aligned in light
        // space) largely does not. Report the centre pixel before/after.
        {
            ShadowMap maps1 = renderMaps({ground, occNear});
            DrawSubmission sub = ground;
            sub.shadow = &maps1;
            backend->clear();
            backend->submit(sub);
            backend->readback();
            // Measure over a 7x7 grid around the centre (crosses the shadow
            // edge), not just the centre pixel.
            auto gridSum = [&]() {
                backend->clear();
                backend->submit(sub);
                for (int gy = -3; gy <= 3; ++gy)
                    for (int gx = -3; gx <= 3; ++gx)
                        backend->queueReadback(
                            static_cast<std::uint32_t>(64 + gx * 8),
                            static_cast<std::uint32_t>(48 + gy * 8));
                backend->readback();
                int sum = 0;
                for (const auto& s : backend->lastReadback())
                    sum += static_cast<int>(s.color.r);
                return sum;
            };
            const int sum1 = gridSum();
            grace::render::CameraParams moved = backend->camera();
            moved.eye.x += 0.02f;
            backend->setCamera(moved);
            ShadowMap maps2 = renderMaps({ground, occNear});
            DrawSubmission sub2 = ground;
            sub2.shadow = &maps2;
            const int sum2 = gridSum();
            backend->setCamera(camera);  // restore
            std::printf("%s temporal stability: camera +0.02x -> 7x7 grid sum %d -> %d (delta %d)\n",
                        modeName[mode], sum1, sum2, std::abs(sum1 - sum2));
            releaseMaps(maps1);
            releaseMaps(maps2);
        }
    }

    for (RenderTarget* t : targets)
        backend->releaseRenderTarget(t);

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("tsm");
}
