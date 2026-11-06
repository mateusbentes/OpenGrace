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
using grace::render::DirectionalLight;
using grace::render::DrawSubmission;
using grace::render::Material;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::PixelFormat;
using grace::render::RenderTarget;
using grace::render::RenderTargetDesc;
using grace::render::SceneBounds;
using grace::render::SurfaceParams;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;

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
    config.shadow_map_size = 256;  // fixed documented resolution, from config

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
    gpu_mesh->upload(makeGroundMesh());

    // Depth-only shadow target at the configured fixed resolution.
    RenderTargetDesc shadow_desc;
    shadow_desc.width = config.shadow_map_size;
    shadow_desc.height = config.shadow_map_size;
    shadow_desc.colorAttachment = false;  // depth-only: what a shadow pass needs
    shadow_desc.depthAttachment = true;
    shadow_desc.depthFormat = PixelFormat::Depth24Stencil8;
    RenderTarget* shadow_target = backend->createRenderTarget(shadow_desc);
    GRACE_CHECK(shadow_target != nullptr);
    GRACE_CHECK(shadow_target->hasDepthAttachment());
    GRACE_CHECK(!shadow_target->hasColorAttachment());

    const ClearColor black{0, 0, 0, 255};
    backend->setClearColor(black);

    SurfaceParams surface;  // albedo white, no specular, no emissive
    surface.albedo = vec3{1.0f, 1.0f, 1.0f};

    DirectionalLight sun;
    sun.direction = vec3{0.0f, 0.0f, -1.0f};  // light travelling towards -Z
    sun.color = vec3{0.5f, 0.5f, 0.5f};
    sun.intensity = 1.0f;

    SceneBounds bounds;
    bounds.center = vec3{0.0f, 0.0f, 0.25f};
    bounds.radius = 1.5f;

    DrawSubmission ground;
    ground.mesh = gpu_mesh.get();
    ground.material.setSurfaceParams(surface);
    ground.sun = sun;

    DrawSubmission occluder = ground;  // same triangle, lifted towards the sun
    occluder.model = grace::core::translation(vec3{0.0f, 0.0f, 0.5f});

    // Case A: surface facing the sun, nothing between it and the sun -> lit.
    {
        std::vector<DrawSubmission> casters;
        casters.push_back(ground);
        backend->renderShadowMap(shadow_target, sun, bounds, casters);
        backend->clear();
        backend->submit(ground);
        backend->readback();
        const ClearColor expected = toColor(0.5f, 0.5f, 0.5f);
        printPixel("lit", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Case B: an occluder between the surface and the sun -> darker (shadowed).
    {
        std::vector<DrawSubmission> casters;
        casters.push_back(ground);
        casters.push_back(occluder);
        backend->renderShadowMap(shadow_target, sun, bounds, casters);
        backend->clear();
        backend->submit(ground);
        backend->readback();
        const ClearColor expected_shadowed = toColor(0.0f, 0.0f, 0.0f);
        printPixel("occluded", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected_shadowed, 2));
    }

    // Case C: sun disabled -> lit only by its other terms (emissive here).
    {
        SurfaceParams emissive_surface = surface;
        emissive_surface.emissive = vec3{0.25f, 0.5f, 0.75f};
        DrawSubmission no_sun;
        no_sun.mesh = gpu_mesh.get();
        no_sun.material.setSurfaceParams(emissive_surface);
        backend->clear();
        backend->submit(no_sun);
        backend->readback();
        const ClearColor expected = toColor(0.25f, 0.5f, 0.75f);
        printPixel("no-sun emissive-only", backend->centerPixel());
        GRACE_CHECK(withinTol(backend->centerPixel(), expected, 2));
    }

    // Misuse: shadow pass into a released or non-depth target is a violation.
    {
        const std::uint64_t before = backend->violationCount();
        backend->renderShadowMap(shadow_target, sun, bounds, {});  // empty list is valid
        GRACE_CHECK_EQ(backend->violationCount(), before);
        backend->releaseRenderTarget(shadow_target);
        backend->renderShadowMap(shadow_target, sun, bounds, {});  // after release
        GRACE_CHECK_EQ(backend->violationCount(), before + 1u);
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("shadows");
}
