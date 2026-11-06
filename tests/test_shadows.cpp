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
using grace::render::CascadeConfig;
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

ClearColor toColor(float r, float g, float b)
{
    const auto to8 = [](float v) {
        const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
    };
    return ClearColor{to8(r), to8(g), to8(b), 255};
}

// Flat triangle in the z=0 plane facing +Z (towards the diagnostic camera).
Mesh makeGroundMesh()
{
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 6 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    layout.attributes.push_back(VertexAttribute{VertexSemantic::Normal, ComponentType::Float32,
                                                3, 3 * sizeof(float), false});
    mesh.setLayout(layout);
    const vec3 normal{0.0f, 0.0f, 1.0f};
    mesh.setVertexData(std::vector<float>{
        -0.5f, -0.5f, 0.0f, normal.x, normal.y, normal.z,  //
        0.5f, -0.5f, 0.0f, normal.x, normal.y, normal.z,   //
        0.0f, 0.5f, 0.0f, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2});
    return mesh;
}

void printPixel(const char* label, ClearColor pixel)
{
    std::printf("shadow check %s: pixel=%d,%d,%d,%d\n", label, static_cast<int>(pixel.r),
                static_cast<int>(pixel.g), static_cast<int>(pixel.b), static_cast<int>(pixel.a));
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

    // Camera with a 20-unit far plane so near and far cases land in different
    // cascades (splits are derived from these parameters).
    grace::render::CameraParams camera = backend->camera();
    camera.far = 20.0f;
    backend->setCamera(camera);

    auto gpu_mesh = backend->createGpuMesh();
    gpu_mesh->upload(makeGroundMesh());

    const ClearColor black{0, 0, 0, 255};
    backend->setClearColor(black);

    SurfaceParams surface;  // albedo white, no specular, no emissive
    surface.albedo = vec3{1.0f, 1.0f, 1.0f};

    DirectionalLight sun;
    sun.direction = vec3{0.0f, 0.0f, -1.0f};  // light travelling towards -Z
    sun.color = vec3{0.5f, 0.5f, 0.5f};
    sun.intensity = 1.0f;

    DrawSubmission ground;
    ground.mesh = gpu_mesh.get();
    ground.material.setSurfaceParams(surface);
    ground.sun = sun;

    DrawSubmission occluder_near = ground;  // between the sun and the ground, close to the camera
    occluder_near.model = grace::core::translation(vec3{0.0f, 0.0f, 0.5f});

    DrawSubmission ground_far = ground;  // same surface far from the camera
    ground_far.model = grace::core::translation(vec3{0.0f, 0.0f, -8.0f});
    DrawSubmission occluder_far = ground_far;  // between the sun and the far surface
    occluder_far.model = grace::core::translation(vec3{0.0f, 0.0f, -7.0f});

    CascadeConfig cascade_config;
    cascade_config.count = 4;
    cascade_config.mapSize = 256;

    std::vector<RenderTarget*> all_targets;

    auto renderShadow = [&](const std::vector<DrawSubmission>& casters, bool debug) {
        CascadeConfig cc = cascade_config;
        cc.debug = debug;
        ShadowMap map = renderShadowCascades(*backend, sun, backend->camera(), casters, cc);
        for (RenderTarget* t : map.targets)
            all_targets.push_back(t);
        return map;
    };

    // Case A: surface facing the sun, nothing between it and the sun -> lit.
    {
        ShadowMap shadow = renderShadow({ground}, false);
        DrawSubmission sub = ground;
        sub.shadow = &shadow;
        backend->clear();
        backend->submit(sub);
        backend->readback();
        const ClearColor expected = toColor(0.5f, 0.5f, 0.5f);
        printPixel("lit (no occluder)", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Case B: occluder close to the camera casts a shadow on the near surface.
    {
        ShadowMap shadow = renderShadow({ground, occluder_near}, false);
        DrawSubmission sub = ground;
        sub.shadow = &shadow;
        backend->clear();
        backend->submit(sub);
        backend->readback();
        const ClearColor expected = toColor(0.0f, 0.0f, 0.0f);
        printPixel("occluded (near occluder)", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Case C: the same occluder far from the camera casts a shadow on the far
    // surface — a different cascade; the single 4b map could not cover both
    // reliably at one resolution.
    {
        ShadowMap shadow_lit = renderShadow({ground_far}, false);
        DrawSubmission sub_lit = ground_far;
        sub_lit.shadow = &shadow_lit;
        backend->clear();
        backend->submit(sub_lit);
        backend->readback();
        const ClearColor expected_lit = toColor(0.5f, 0.5f, 0.5f);
        printPixel("far surface lit", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected_lit, 2));

        ShadowMap shadow_occ = renderShadow({ground_far, occluder_far}, false);
        DrawSubmission sub_occ = ground_far;
        sub_occ.shadow = &shadow_occ;
        backend->clear();
        backend->submit(sub_occ);
        backend->readback();
        const ClearColor expected_occ = toColor(0.0f, 0.0f, 0.0f);
        printPixel("far surface occluded (far occluder)", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected_occ, 2));
    }

    // Case D: sun disabled -> lit only by its other terms (emissive here).
    {
        SurfaceParams emissive_surface = surface;
        emissive_surface.emissive = vec3{0.25f, 0.5f, 0.75f};
        DrawSubmission sub;
        sub.mesh = gpu_mesh.get();
        sub.material.setSurfaceParams(emissive_surface);
        backend->clear();
        backend->submit(sub);
        backend->readback();
        const ClearColor expected = toColor(0.25f, 0.5f, 0.75f);
        printPixel("no-sun emissive-only", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Case E: diagnostics (C5) — cascade-index visualisation, off by default.
    {
        ShadowMap shadow = renderShadow({ground}, true);  // debug = true
        DrawSubmission sub = ground;
        sub.shadow = &shadow;
        backend->clear();
        backend->submit(sub);
        backend->readback();
        // Ground centre is at view depth ~1 -> cascade 0 of 4 -> (0, 0, 1).
        const ClearColor expected = toColor(0.0f, 0.0f, 1.0f);
        printPixel("debug cascade index", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Misuse: beginning a pass on a released target is a violation.
    {
        const std::uint64_t before = backend->violationCount();
        for (RenderTarget* t : all_targets)
            backend->releaseRenderTarget(t);
        backend->beginRenderPass(all_targets[0]);
        GRACE_CHECK_EQ(backend->violationCount(), before + 1u);
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("shadows");
}
