#include "grace/render/camera.hpp"
#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/renderer.hpp"

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
using grace::render::DrawSubmission;
using grace::render::Material;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::RenderPath;
using grace::render::Renderer;
using grace::render::RendererConfig;
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

// Expected chain output for a linear input L (exposure 1, no bloom):
// sRGB(aces(L)) with the ACES approximation, 8-bit.
std::uint8_t expectedTonemap(float linear)
{
    const float x = linear;
    float aces = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    if (aces < 0.0f)
        aces = 0.0f;
    if (aces > 1.0f)
        aces = 1.0f;
    const float srgb = aces <= 0.0031308f ? aces * 12.92f
                                          : 1.055f * std::pow(aces, 1.0f / 2.4f) - 0.055f;
    const float clamped = srgb < 0.0f ? 0.0f : (srgb > 1.0f ? 1.0f : srgb);
    return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
}

ClearColor runScene(OpenGLRenderBackend& backend, Renderer& renderer, const DrawSubmission& sub)
{
    renderer.beginFrame();
    renderer.submit(sub);
    // Queue the readback BEFORE endFrame: swapBuffers (decision 16) captures
    // queued readbacks pre-swap; the back buffer is undefined after it.
    backend.queueReadback(backend.width() / 2u, backend.height() / 2u);
    renderer.endFrame();
    return backend.centerPixel();  // cached pre-swap value
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
    groundMesh->upload(makeGroundMesh());

    const ClearColor black{0, 0, 0, 255};
    backend->setClearColor(black);

    // A flat emissive surface with a known linear value, no lights: the
    // lighting output equals the emissive value exactly.
    auto flatScene = [&](float emissive) {
        DrawSubmission sub;
        sub.mesh = groundMesh.get();
        SurfaceParams surface;
        surface.albedo = vec3{0.0f, 0.0f, 0.0f};
        surface.emissive = vec3{emissive, emissive, emissive};
        sub.material.setSurfaceParams(surface);
        return sub;
    };

    // H2: tonemap response — known linear inputs map to known display outputs.
    {
        Renderer renderer(*backend);
        RendererConfig cfg;
        cfg.path = RenderPath::Forward;
        cfg.shadows = false;
        cfg.clear_color = black;
        cfg.post.enabled = true;
        cfg.post.bloomThreshold = 1000.0f;  // bloom effectively off for the response test
        renderer.setConfig(cfg);
        for (const float input : {0.0f, 0.18f, 0.5f, 1.0f, 2.0f, 4.0f})
        {
            const ClearColor p = runScene(*backend, renderer, flatScene(input));
            const std::uint8_t expected = expectedTonemap(input);
            std::printf("tonemap response: linear %.2f -> pixel %d (expected %d)\n", input,
                        static_cast<int>(p.r), static_cast<int>(expected));
            GRACE_CHECK(withinTol(p, ClearColor{expected, expected, expected, 255}, 2));
        }
    }

    // H4: with post OFF, the same scene produces the legacy linear->8bit value
    // (bit-identical to before the chain existed).
    {
        Renderer renderer(*backend);
        RendererConfig cfg;
        cfg.path = RenderPath::Forward;
        cfg.shadows = false;
        cfg.clear_color = black;
        cfg.post.enabled = false;
        renderer.setConfig(cfg);
        const ClearColor p = runScene(*backend, renderer, flatScene(0.5f));
        std::printf("post off: linear 0.5 -> pixel %d (legacy linear->8bit, expected 128)\n",
                    static_cast<int>(p.r));
        GRACE_CHECK(withinTol(p, ClearColor{128, 128, 128, 255}, 2));
    }

    // H3: bloom halo — a bright emissive spot (4.0 > threshold 1.0) on black.
    // Measure the pixel at a known distance from the centre (16 px right),
    // with bloom on vs off.
    {
        // small bright triangle at the centre
        Mesh spot;
        VertexLayout layout;
        layout.stride = 6 * sizeof(float);
        layout.attributes.push_back(
            VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
        layout.attributes.push_back(VertexAttribute{VertexSemantic::Normal, ComponentType::Float32,
                                                    3, 3 * sizeof(float), false});
        spot.setLayout(layout);
        const vec3 normal{0.0f, 0.0f, 1.0f};
        // a small triangle around NDC (0,0)
        spot.setVertexData(std::vector<float>{-0.1f, -0.1f, 0.0f, normal.x, normal.y, normal.z,
                                              0.1f, -0.1f, 0.0f, normal.x, normal.y, normal.z,
                                              0.0f, 0.1f, 0.0f, normal.x, normal.y, normal.z});
        spot.setIndices(std::vector<std::uint32_t>{0, 1, 2});
        auto spotMesh = backend->createGpuMesh();
        spotMesh->upload(spot);

        DrawSubmission spotSub;
        spotSub.mesh = spotMesh.get();
        SurfaceParams surface;
        surface.albedo = vec3{0.0f, 0.0f, 0.0f};
        surface.emissive = vec3{4.0f, 4.0f, 4.0f};  // above the 1.0 threshold
        spotSub.material.setSurfaceParams(surface);

        // bloom ON
        {
            Renderer renderer(*backend);
            RendererConfig cfg;
            cfg.path = RenderPath::Forward;
            cfg.shadows = false;
            cfg.clear_color = black;
            cfg.post.enabled = true;
            cfg.post.bloomThreshold = 1.0f;
            renderer.setConfig(cfg);
            renderer.beginFrame();
            renderer.submit(spotSub);
            backend->queueReadback(64, 48);
            backend->queueReadback(68, 48);
            backend->queueReadback(72, 48);
            renderer.endFrame();  // captures the queued readbacks pre-swap
            const ClearColor centre = backend->lastReadback()[0].color;
            const ClearColor halo4 = backend->lastReadback()[1].color;
            const ClearColor halo8 = backend->lastReadback()[2].color;
            std::printf("bloom on:  centre=%d  halo@4px=%d  halo@8px=%d\n",
                        static_cast<int>(centre.r), static_cast<int>(halo4.r),
                        static_cast<int>(halo8.r));
            GRACE_CHECK(halo4.r > 0);  // the halo is measurable 4 px from the source
            GRACE_CHECK(halo4.r <= centre.r);
        }
        // bloom OFF (post disabled): same scene, legacy output, no halo.
        {
            Renderer renderer(*backend);
            RendererConfig cfg;
            cfg.path = RenderPath::Forward;
            cfg.shadows = false;
            cfg.clear_color = black;
            cfg.post.enabled = false;
            renderer.setConfig(cfg);
            renderer.beginFrame();
            renderer.submit(spotSub);
            backend->queueReadback(64, 48);
            backend->queueReadback(68, 48);
            renderer.endFrame();
            const ClearColor centre = backend->lastReadback()[0].color;
            const ClearColor halo4 = backend->lastReadback()[1].color;
            std::printf("bloom off: centre=%d  halo@4px=%d (legacy linear->8bit)\n",
                        static_cast<int>(centre.r), static_cast<int>(halo4.r));
            GRACE_CHECK_EQ(halo4.r, 0u);  // no halo when off
        }
    }

    // H4: both paths feed the same chain — deferred with post on produces the
    // tonemapped value too. Sun-lit ground at 0.5 linear (the deferred G-buffer
    // has no emissive channel yet, so a lit scene is used here).
    {
        Renderer renderer(*backend);
        RendererConfig cfg;
        cfg.path = RenderPath::Deferred;
        cfg.clear_color = black;
        cfg.post.enabled = true;
        cfg.post.bloomThreshold = 1000.0f;
        renderer.setConfig(cfg);
        DrawSubmission sunSub;
        sunSub.mesh = groundMesh.get();
        SurfaceParams surface;
        surface.albedo = vec3{1.0f, 1.0f, 1.0f};
        sunSub.material.setSurfaceParams(surface);
        grace::render::DirectionalLight sun;
        sun.direction = vec3{0.0f, 0.0f, -1.0f};
        sun.color = vec3{0.5f, 0.5f, 0.5f};
        sunSub.sun = sun;
        const ClearColor p = runScene(*backend, renderer, sunSub);
        // deferred+post now includes the scene ambient (0.05) on top of the sun
        const std::uint8_t expected = expectedTonemap(0.5f + cfg.ambient);
        std::printf("deferred + post: linear 0.5 (sun) + ambient 0.05 -> pixel %d (expected %d)\n",
                    static_cast<int>(p.r), static_cast<int>(expected));
        GRACE_CHECK(withinTol(p, ClearColor{expected, expected, expected, 255}, 3));
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("post");
}
