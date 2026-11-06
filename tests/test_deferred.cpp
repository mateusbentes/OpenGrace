#include "grace/render/camera.hpp"
#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/renderer.hpp"
#include "grace/render/render_target.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::vec3;
using grace::render::ClearColor;
using grace::render::ComponentType;
using grace::render::DirectionalLight;
using grace::render::DrawSubmission;
using grace::render::DynamicLight;
using grace::render::kDepthAttachment;
using grace::render::kMaxColorAttachments;
using grace::render::kMaxDeferredLights;
using grace::render::Material;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::PixelFormat;
using grace::render::PointLight;
using grace::render::RenderPath;
using grace::render::Renderer;
using grace::render::RendererConfig;
using grace::render::RenderTarget;
using grace::render::RenderTargetDesc;
using grace::render::SurfaceParams;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;
using grace::render::pointAttenuation;

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

    // D3: path selection lives in the renderer now; default forward; invalid rejected.
    {
        Renderer renderer(*backend);
        GRACE_CHECK(renderer.config().path == RenderPath::Forward);
        RendererConfig rc;
        rc.path = RenderPath::Deferred;
        renderer.setConfig(rc);
        GRACE_CHECK(renderer.config().path == RenderPath::Deferred);
        RendererConfig bad;
        bad.path = static_cast<RenderPath>(99);
        GRACE_CHECK(throwsInvalid([&] { renderer.setConfig(bad); }));
    }

    // D0: MRT descriptor — cap enforced, per-attachment views.
    GRACE_CHECK_EQ(kMaxColorAttachments, 4u);
    {
        RenderTargetDesc mrt;
        mrt.width = 64;
        mrt.height = 64;
        mrt.colorAttachmentCount = 2;
        mrt.colorFormats[0] = PixelFormat::Rgba8;
        mrt.colorFormats[1] = PixelFormat::Rgba8;
        mrt.depthAttachment = true;
        RenderTarget* t = backend->createRenderTarget(mrt);
        GRACE_CHECK_EQ(t->colorAttachmentCount(), 2u);
        GRACE_CHECK(t->hasColorAttachment());
        GRACE_CHECK(t->hasDepthAttachment());
        GRACE_CHECK(backend->renderTargetTexture(t, 0) != nullptr);
        GRACE_CHECK(backend->renderTargetTexture(t, 1) != nullptr);
        GRACE_CHECK(backend->renderTargetTexture(t, 1)->isDepth() == false);
        GRACE_CHECK(backend->renderTargetTexture(t, kDepthAttachment) != nullptr);
        GRACE_CHECK(backend->renderTargetTexture(t, kDepthAttachment)->isDepth());
        GRACE_CHECK(backend->renderTargetTexture(t, 2) == nullptr);  // out of range: violation
        GRACE_CHECK(backend->renderTargetTexture(t, 99) == nullptr);
        RenderTargetDesc over;
        over.width = 8;
        over.height = 8;
        over.colorAttachmentCount = kMaxColorAttachments + 1;
        GRACE_CHECK(throwsInvalid([&] { static_cast<void>(backend->createRenderTarget(over)); }));
        backend->releaseRenderTarget(t);
    }

    auto groundMesh = backend->createGpuMesh();
    groundMesh->upload(makeGroundMesh());

    const ClearColor black{0, 0, 0, 255};
    backend->setClearColor(black);

    SurfaceParams surface;
    surface.albedo = vec3{1.0f, 1.0f, 1.0f};
    surface.shininess = 64.0f;

    DirectionalLight sun;
    sun.direction = vec3{0.0f, 0.0f, -1.0f};
    sun.color = vec3{0.5f, 0.5f, 0.5f};
    sun.intensity = 1.0f;

    DrawSubmission ground;
    ground.mesh = groundMesh.get();
    ground.material.setSurfaceParams(surface);
    ground.sun = sun;

    // D4 parity: same scene through forward and deferred, both driven by a
    // Renderer. (Before 5b the deferred path was driven by direct contract
    // calls and produced centre 127,127,127,255.)
    {
        Renderer fwdRenderer(*backend);
        RendererConfig fwdCfg;
        fwdCfg.path = RenderPath::Forward;
        fwdCfg.shadows = false;
        fwdCfg.clear_color = black;
        fwdRenderer.setConfig(fwdCfg);
        fwdRenderer.beginFrame();
        fwdRenderer.submit(ground);
        fwdRenderer.endFrame();
        backend->readback();
        const ClearColor fwd = backend->centerPixel();

        Renderer defRenderer(*backend);
        RendererConfig defCfg;
        defCfg.path = RenderPath::Deferred;
        defCfg.clear_color = black;
        defRenderer.setConfig(defCfg);
        defRenderer.beginFrame();
        defRenderer.submit(ground);
        defRenderer.endFrame();
        // D5: G-buffer attachment 0 contents via readback of the renderer's
        // G-buffer target (albedo rgb + shininess/1024 in a).
        backend->beginRenderPass(defRenderer.gbufferTarget());
        backend->readback();
        const ClearColor gbuf0 = backend->centerPixel();
        backend->endRenderPass();
        std::printf("g-buffer attachment 0 centre: %d,%d,%d,%d (albedo 255,255,255, a=shininess/1024*255~16)\n",
                    static_cast<int>(gbuf0.r), static_cast<int>(gbuf0.g),
                    static_cast<int>(gbuf0.b), static_cast<int>(gbuf0.a));
        GRACE_CHECK(withinTol(gbuf0, ClearColor{255, 255, 255, 16}, 2));
        backend->readback();
        const ClearColor def = backend->centerPixel();
        std::printf("parity: forward centre=%d,%d,%d,%d  deferred centre=%d,%d,%d,%d  diff=%d\n",
                    static_cast<int>(fwd.r), static_cast<int>(fwd.g), static_cast<int>(fwd.b),
                    static_cast<int>(fwd.a), static_cast<int>(def.r), static_cast<int>(def.g),
                    static_cast<int>(def.b), static_cast<int>(def.a),
                    std::abs(static_cast<int>(fwd.r) - static_cast<int>(def.r)));
        GRACE_CHECK(withinTol(def, fwd, 3));
    }

    // D5: deferred lights beyond the forward per-object limit of 4. Six point
    // lights around the ground; forward rejects the same submission.
    {
        GRACE_CHECK_EQ(kMaxDeferredLights, 16u);
        std::vector<DynamicLight> six;
        for (int i = 0; i < 6; ++i)
        {
            PointLight p;
            p.position = vec3{0.0f, 0.0f, 1.0f + 0.2f * static_cast<float>(i)};
            p.color = vec3{0.2f, 0.2f, 0.2f};
            p.intensity = 1.0f;
            p.radius = 10.0f;
            six.push_back(p);
        }
        DrawSubmission many = ground;
        many.lights = six;
        // Forward: 6 dynamic lights exceed the per-object limit -> rejected.
        Renderer fwdRenderer(*backend);
        RendererConfig fwdCfg;
        fwdCfg.path = RenderPath::Forward;
        fwdCfg.shadows = false;
        fwdCfg.clear_color = black;
        fwdRenderer.setConfig(fwdCfg);
        GRACE_CHECK(throwsInvalid([&] {
            fwdRenderer.beginFrame();
            fwdRenderer.submit(many);  // validate() rejects > 4 in forward
        }));

        // Deferred: accepted; the lighting pass applies all six.
        Renderer defRenderer(*backend);
        RendererConfig defCfg;
        defCfg.path = RenderPath::Deferred;
        defCfg.clear_color = black;
        defRenderer.setConfig(defCfg);
        defRenderer.beginFrame();
        defRenderer.submit(many);
        defRenderer.endFrame();
        backend->readback();
        const ClearColor p = backend->centerPixel();
        float expected = 0.5f;  // the sun (ground has sun, ndl = 1 at the centre)
        for (int i = 0; i < 6; ++i)
        {
            const float dist = 1.0f + 0.2f * static_cast<float>(i);
            expected += pointAttenuation(std::get<PointLight>(six[static_cast<std::size_t>(i)]), dist) * 0.2f;
        }
        const ClearColor expectedColor = ClearColor{
            static_cast<std::uint8_t>(std::fmin(expected, 1.0f) * 255.0f + 0.5f),
            static_cast<std::uint8_t>(std::fmin(expected, 1.0f) * 255.0f + 0.5f),
            static_cast<std::uint8_t>(std::fmin(expected, 1.0f) * 255.0f + 0.5f), 255};
        std::printf("deferred 6 lights: centre pixel=%d,%d,%d,%d (expected ~%d)\n",
                    static_cast<int>(p.r), static_cast<int>(p.g), static_cast<int>(p.b),
                    static_cast<int>(p.a), static_cast<int>(expectedColor.r));
        GRACE_CHECK(withinTol(p, expectedColor, 3));
    }

    // G1: parity on a scene WITH shadow-casting geometry — forward and
    // deferred both consume the cascade sun shadow map (since 5c).
    {
        DrawSubmission occ = ground;
        occ.model = grace::core::translation(vec3{0.0f, 0.0f, 0.5f});  // between sun and ground
        occ.castOnly = true;  // casts the shadow, is not drawn

        Renderer fwdRenderer(*backend);
        RendererConfig fwdCfg;
        fwdCfg.path = RenderPath::Forward;
        fwdCfg.shadows = true;
        fwdCfg.clear_color = black;
        fwdCfg.camera.far = 20.0f;
        fwdCfg.cascades.mapSize = 256;
        fwdRenderer.setConfig(fwdCfg);
        fwdRenderer.beginFrame();
        fwdRenderer.submit(ground);
        fwdRenderer.submit(occ);
        fwdRenderer.endFrame();
        backend->readback();
        const ClearColor fwdShadow = backend->centerPixel();

        Renderer defRenderer(*backend);
        RendererConfig defCfg;
        defCfg.path = RenderPath::Deferred;
        defCfg.shadows = true;
        defCfg.clear_color = black;
        defCfg.camera.far = 20.0f;
        defCfg.cascades.mapSize = 256;
        defRenderer.setConfig(defCfg);
        defRenderer.beginFrame();
        defRenderer.submit(ground);
        defRenderer.submit(occ);
        defRenderer.endFrame();
        backend->readback();
        const ClearColor defShadow = backend->centerPixel();
        std::printf("shadow parity: forward centre=%d,%d,%d,%d  deferred centre=%d,%d,%d,%d  diff=%d\n",
                    static_cast<int>(fwdShadow.r), static_cast<int>(fwdShadow.g),
                    static_cast<int>(fwdShadow.b), static_cast<int>(fwdShadow.a),
                    static_cast<int>(defShadow.r), static_cast<int>(defShadow.g),
                    static_cast<int>(defShadow.b), static_cast<int>(defShadow.a),
                    std::abs(static_cast<int>(fwdShadow.r) - static_cast<int>(defShadow.r)));
        // The occluder shadows the centre in both paths (sun term zeroed).
        GRACE_CHECK(withinTol(fwdShadow, ClearColor{0, 0, 0, 255}, 2));
        GRACE_CHECK(withinTol(defShadow, ClearColor{0, 0, 0, 255}, 2));
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("deferred");
}
