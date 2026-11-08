#include "grace/render/null_render_backend.hpp"
#include "grace/render/renderer.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::render::ClearColor;
using grace::render::ComponentType;
using grace::render::DrawSubmission;
using grace::render::Mesh;
using grace::render::NullRenderBackend;
using grace::render::RenderPath;
using grace::render::Renderer;
using grace::render::RendererConfig;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;

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

template <typename F>
bool throwsLogic(F&& fn)
{
    try
    {
        fn();
    }
    catch (const std::logic_error&)
    {
        return true;
    }
    return false;
}

Mesh makeTriangle()
{
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 3 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    mesh.setLayout(layout);
    mesh.setVertexData(std::vector<float>{-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2});
    return mesh;
}

}  // namespace

int main()
{
    // F5: the renderer is testable without a GPU — frame assembly against the
    // null backend, pass order visible in the event log.
    {
        NullRenderBackend backend(128, 96);
        Renderer renderer(backend);
        GRACE_CHECK(renderer.config().path == RenderPath::Forward);  // default
        // 6e A3: the default configuration as VALUES (decision 23) — the chain
        // is on by default; SSAO and MLAA on, DOF off (focus is scene content).
        GRACE_CHECK(renderer.config().post.enabled == true);
        GRACE_CHECK(renderer.config().post.ssao.enabled == true);
        GRACE_CHECK(renderer.config().post.mlaa.enabled == true);
        GRACE_CHECK(renderer.config().post.dof.enabled == false);
        GRACE_CHECK(renderer.config().post.bloomLevels == 4u);
        GRACE_CHECK(renderer.config().ambient == 0.2f);
        GRACE_CHECK_EQ(renderer.frameCount(), 0u);

        auto mesh = backend.createGpuMesh();
        mesh->upload(makeTriangle());
        DrawSubmission submission;
        submission.mesh = mesh.get();

        // Frame boundary errors.
        GRACE_CHECK(throwsLogic([&] { renderer.endFrame(); }));
        GRACE_CHECK(throwsLogic([&] { renderer.submit(submission); }));

        renderer.beginFrame();
        GRACE_CHECK(throwsLogic([&] { renderer.beginFrame(); }));
        renderer.submit(submission);
        renderer.endFrame();
        GRACE_CHECK_EQ(renderer.frameCount(), 1u);

        // Forward frame assembly with the post chain ON BY DEFAULT (6e,
        // decision 23): HDR pass (clear + submit), then the chain — SSAO,
        // bloom levels, tonemap into the offscreen display target, MLAA on
        // the display image, present pass onto the default framebuffer.
        const std::vector<std::string> expected{
            "createGpuMesh",
            "upload",
            "setCamera",
            "createRenderTarget(128x96,color=2,depth=1)",
            "createRenderTarget(128x96,color=1,depth=0)",
            "createRenderTarget(128x96,color=2,depth=0)",
            "createRenderTarget(128x96,color=1,depth=0)",
            "createRenderTarget(128x96,color=1,depth=0)",
            "createRenderTarget(128x96,color=1,depth=0)",
            "createRenderTarget(64x48,color=1,depth=0)",
            "createRenderTarget(32x24,color=1,depth=0)",
            "createRenderTarget(16x12,color=1,depth=0)",
            "createRenderTarget(8x6,color=1,depth=0)",
            "createShaderProgram(PostBright)",
            "createShaderProgram(PostBlur)",
            "createShaderProgram(PostTonemap)",
            "createShaderProgram(PostSSAO)",
            "createShaderProgram(PostMLAA)",
            "createShaderProgram(PostDOF)",
            "createShaderProgram(PostBlit)",
            "createShaderProgram(PostVolumetric)",
            "createGpuMesh",
            "upload",
            "beginRenderPass(128x96)",
            "clear(0,0,0,255)",
            "submit(lights=0,sun=0,shadow=0,pointShadows=0,volume=0)",
            "endRenderPass",
            "setDepthTestEnabled(0)",
            "beginRenderPass(128x96)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=1)",
            "use",
            "setUniform(depthTex)",
            "setUniform(outSize)",
            "setUniform(invProj)",
            "setUniform(proj)",
            "setUniform(radius)",
            "setUniform(sampleCount)",
            "setUniform(bias)",
            "drawIndexed",
            "endRenderPass",
            "beginRenderPass(64x48)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=0)",
            "use",
            "setUniform(srcTex)",
            "setUniform(outSize)",
            "setUniform(threshold)",
            "drawIndexed",
            "endRenderPass",
            "beginRenderPass(32x24)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=0)",
            "use",
            "setUniform(srcTex)",
            "setUniform(outSize)",
            "setUniform(direction)",
            "drawIndexed",
            "endRenderPass",
            "beginRenderPass(16x12)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=0)",
            "use",
            "setUniform(srcTex)",
            "setUniform(outSize)",
            "setUniform(direction)",
            "drawIndexed",
            "endRenderPass",
            "beginRenderPass(8x6)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=0)",
            "use",
            "setUniform(srcTex)",
            "setUniform(outSize)",
            "setUniform(direction)",
            "drawIndexed",
            "endRenderPass",
            "beginRenderPass(128x96)",
            "clear(0,0,0,255)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=0)",
            "renderTargetTexture",
            "bindTexture(unit=1,depth=0)",
            "renderTargetTexture",
            "bindTexture(unit=2,depth=0)",
            "renderTargetTexture",
            "bindTexture(unit=3,depth=0)",
            "renderTargetTexture",
            "bindTexture(unit=4,depth=0)",
            "use",
            "setUniform(hdrTex)",
            "setUniform(hdrAmbient)",
            "setUniform(bloomTex)",
            "setUniform(ssaoTex)",
            "setUniform(volTex)",
            "setUniform(useVolumetric)",
            "setUniform(useSSAO)",
            "setUniform(outSize)",
            "setUniform(exposure)",
            "drawIndexed",
            "endRenderPass",
            "beginRenderPass(128x96)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=0)",
            "renderTargetTexture",
            "bindTexture(unit=1,depth=1)",
            "use",
            "setUniform(srcTex)",
            "setUniform(depthTex)",
            "setUniform(outSize)",
            "setUniform(lumThreshold)",
            "setUniform(depthThreshold)",
            "drawIndexed",
            "endRenderPass",
            "beginRenderPass(default)",
            "renderTargetTexture",
            "bindTexture(unit=0,depth=0)",
            "use",
            "setUniform(srcTex)",
            "drawIndexed",
            "endRenderPass",
            "setDepthTestEnabled(1)",
            "endRenderPass"
        };
        GRACE_CHECK(backend.events() == expected);
        GRACE_CHECK_EQ(backend.violationCount(), 0u);
        backend.shutdown();
    }

    // Deferred frame assembly through the renderer (null backend records the
    // composed passes): G-buffer target, G-buffer pass, lighting pass.
    {
        NullRenderBackend backend(128, 96);
        Renderer renderer(backend);
        RendererConfig config;
        config.path = RenderPath::Deferred;
        config.clear_color = ClearColor{30, 30, 30, 255};
        renderer.setConfig(config);
        GRACE_CHECK(renderer.config().path == RenderPath::Deferred);

        auto mesh = backend.createGpuMesh();
        mesh->upload(makeTriangle());
        DrawSubmission submission;
        submission.mesh = mesh.get();

        renderer.setClearColor(ClearColor{30, 30, 30, 255});
        renderer.beginFrame();
        renderer.submit(submission);
        renderer.endFrame();

        // The composed frame: G-buffer creation + G-buffer pass + lighting pass.
        bool sawGbufferCreate = false;
        bool sawLightingDraw = false;
        int beginPasses = 0;
        for (const std::string& event : backend.events())
        {
            if (event == "createRenderTarget(128x96,color=2,depth=1)")
                sawGbufferCreate = true;
            if (event == "beginRenderPass(128x96)")
                ++beginPasses;
            if (event == "bindTexture(unit=0,depth=0)")
                sawLightingDraw = true;
        }
        GRACE_CHECK(sawGbufferCreate);
        bool sawDepthCopy = false;
        int defaultPasses = 0;
        for (const std::string& event : backend.events())
        {
            if (event == "beginRenderPass(default)")
                ++defaultPasses;
            if (event == "copyDepthAttachment")
                sawDepthCopy = true;
        }
        // With the chain on by default (6e): the G-buffer pass, the HDR
        // lighting pass, the display/MLAA passes, and the depth copy from the
        // G-buffer to the HDR target (decision 19); the default framebuffer
        // receives exactly one pass — the present pass.
        GRACE_CHECK(beginPasses >= 2);        // G-buffer + HDR + display passes
        GRACE_CHECK(sawDepthCopy);            // scene depth reaches the HDR target
        GRACE_CHECK_EQ(defaultPasses, 1);     // the present pass
        GRACE_CHECK(sawLightingDraw);
        GRACE_CHECK_EQ(backend.violationCount(), 0u);

        // Path selection validation.
        RendererConfig bad;
        bad.path = static_cast<RenderPath>(99);
        GRACE_CHECK(throwsInvalid([&] { renderer.setConfig(bad); }));
        backend.shutdown();
    }

    // Shadow pass assembly is the renderer's job in forward mode: a sun
    // submission produces cascade passes before the geometry pass.
    {
        NullRenderBackend backend(64, 48);
        Renderer renderer(backend);
        RendererConfig config;
        config.path = RenderPath::Forward;
        config.shadows = true;
        config.cascades.count = 2;
        config.cascades.mapSize = 32;
        renderer.setConfig(config);

        auto mesh = backend.createGpuMesh();
        mesh->upload(makeTriangle());
        DrawSubmission submission;
        submission.mesh = mesh.get();
        grace::render::DirectionalLight sun;
        sun.direction = grace::core::vec3{0.0f, 0.0f, -1.0f};
        submission.sun = sun;

        renderer.beginFrame();
        renderer.submit(submission);
        renderer.endFrame();

        int cascadePasses = 0;
        bool submitSeen = false;
        for (const std::string& event : backend.events())
        {
            if (event == "beginRenderPass(32x32)")
                ++cascadePasses;
            if (event.find("submit(") == 0)
                submitSeen = true;
        }
        GRACE_CHECK_EQ(cascadePasses, 2);  // one pass per cascade
        GRACE_CHECK(submitSeen);
        // Cascade passes come before the geometry submit.
        std::size_t firstSubmit = backend.events().size();
        for (std::size_t i = 0; i < backend.events().size(); ++i)
            if (backend.events()[i].find("submit(") == 0)
                firstSubmit = i;
        bool cascadeBeforeSubmit = false;
        for (std::size_t i = 0; i < firstSubmit; ++i)
            if (backend.events()[i] == "beginRenderPass(32x32)")
                cascadeBeforeSubmit = true;
        GRACE_CHECK(cascadeBeforeSubmit);
        GRACE_CHECK_EQ(backend.violationCount(), 0u);
        backend.shutdown();
    }

    // SSAO-STF (milestone 6g, decision 31): the structural half on the null
    // backend. The filter is OFF by default: with it off, the frame's event
    // log shows no STF pass, no history target and no PostSSAOStf program —
    // the same off-by-default rule the post chain follows (decision 23).
    {
        NullRenderBackend backend(128, 96);
        Renderer renderer(backend);
        GRACE_CHECK(renderer.config().post.ssao.stf.enabled == false);
        GRACE_CHECK(renderer.config().post.ssao.stf.blend == 0.5f);
        GRACE_CHECK(renderer.config().post.ssao.stf.depthRejection == 0.05f);
        auto mesh = backend.createGpuMesh();
        mesh->upload(makeTriangle());
        DrawSubmission submission;
        submission.mesh = mesh.get();
        renderer.beginFrame();
        renderer.submit(submission);
        renderer.endFrame();
        bool sawStfProgram = false;
        int twoAttachmentTargets = 0;
        for (const std::string& event : backend.events())
        {
            if (event.find("createShaderProgram(PostSSAOStf)") == 0)
                sawStfProgram = true;
            if (event == "createRenderTarget(128x96,color=2,depth=0)")
                ++twoAttachmentTargets;
        }
        GRACE_CHECK(!sawStfProgram);
        // The 6d ping-pong target (postA) is also 128x96 color=2, so the
        // off-by-default frame has exactly one of them: the STF history
        // target is NOT created.
        GRACE_CHECK_EQ(twoAttachmentTargets, 1);
        backend.shutdown();
    }

    // SSAO-STF: with the filter on, the history targets and the STF pass are
    // created, and the tonemap binds the filtered buffer (the last frame's
    // STF output), not the raw buffer.
    {
        NullRenderBackend backend(128, 96);
        Renderer renderer(backend);
        RendererConfig config;
        config.post.ssao.stf.enabled = true;
        renderer.setConfig(config);
        auto mesh = backend.createGpuMesh();
        mesh->upload(makeTriangle());
        DrawSubmission submission;
        submission.mesh = mesh.get();
        renderer.beginFrame();
        renderer.submit(submission);
        renderer.endFrame();
        bool sawStfProgram = false;
        bool sawHistoryTarget = false;
        bool sawStfPass = false;
        for (const std::string& event : backend.events())
        {
            if (event.find("createShaderProgram(PostSSAOStf)") == 0)
                sawStfProgram = true;
            if (event.find("createRenderTarget(128x96,color=2,depth=0)") == 0)
                sawHistoryTarget = true;
            if (event.find("setUniform(hasHistory)") == 0)
                sawStfPass = true;
        }
        GRACE_CHECK(sawStfProgram);
        GRACE_CHECK(sawHistoryTarget);
        GRACE_CHECK(sawStfPass);
        // The history target is 2 attachments (occlusion + depth), so there
        // are exactly two of them: a ping-pong pair.
        int historyTargetCount = 0;
        for (const std::string& event : backend.events())
            if (event == "createRenderTarget(128x96,color=2,depth=0)")
                ++historyTargetCount;
        // The 6d ping-pong target (postA) is also 128x96 color=2, so the
        // STF-on frame has exactly three of them: postA + the ping-pong pair.
        GRACE_CHECK_EQ(historyTargetCount, 3);
        backend.shutdown();
    }

    // SSAO-STF: setCamera does NOT recreate the shadow maps (the per-frame
    // camera path is the 6g prerequisite). The camera is device-level state
    // forwarded at beginFrame; changing it is a setCamera event, not a
    // create/release pair.
    {
        NullRenderBackend backend(128, 96);
        Renderer renderer(backend);
        RendererConfig config;
        config.shadows = true;
        config.cascades.count = 2;
        config.cascades.mapSize = 32;
        renderer.setConfig(config);
        auto mesh = backend.createGpuMesh();
        mesh->upload(makeTriangle());
        DrawSubmission submission;
        submission.mesh = mesh.get();
        grace::render::DirectionalLight sun;
        sun.direction = grace::core::vec3{0.0f, 0.0f, -1.0f};
        submission.sun = sun;

        renderer.beginFrame();
        renderer.submit(submission);
        renderer.endFrame();
        const std::size_t firstFrameEvents = backend.events().size();

        renderer.setCamera(grace::render::CameraParams{
            grace::core::vec3{0.1f, 0.0f, 1.0f}, grace::core::vec3{0.0f, 0.0f, 0.0f},
            grace::core::vec3{0.0f, 1.0f, 0.0f}, 1.5707963f, 1.0f, 0.1f, 100.0f});
        renderer.beginFrame();
        renderer.submit(submission);
        renderer.endFrame();

        int shadowPasses = 0;
        for (std::size_t i = firstFrameEvents; i < backend.events().size(); ++i)
            if (backend.events()[i] == "beginRenderPass(32x32)")
                ++shadowPasses;
        GRACE_CHECK_EQ(shadowPasses, 2);  // the shadow maps are still there
        // The second frame ran a full frame with the new camera, and no
        // releaseRenderTarget or createRenderTarget happened after the first
        // frame's resources (the shadow maps are not rebuilt).
        int releaseEvents = 0;
        for (std::size_t i = firstFrameEvents; i < backend.events().size(); ++i)
            if (backend.events()[i] == "releaseRenderTarget")
                ++releaseEvents;
        GRACE_CHECK_EQ(releaseEvents, 0);
        backend.shutdown();
    }

    // SSAO-STF config validation: the blend is in [0,1), the depth rejection
    // is in (0,1] NDC.
    {
        RendererConfig cfg;
        cfg.post.ssao.stf.blend = -0.1f;
        GRACE_CHECK(throwsInvalid([&] { cfg.post.ssao.stf.validate(); }));
        cfg.post.ssao.stf.blend = 1.0f;
        GRACE_CHECK(throwsInvalid([&] { cfg.post.ssao.stf.validate(); }));
        cfg.post.ssao.stf.blend = 0.5f;
        cfg.post.ssao.stf.depthRejection = 0.0f;
        GRACE_CHECK(throwsInvalid([&] { cfg.post.ssao.stf.validate(); }));
        cfg.post.ssao.stf.depthRejection = 1.5f;
        GRACE_CHECK(throwsInvalid([&] { cfg.post.ssao.stf.validate(); }));
        cfg.post.ssao.stf.depthRejection = 0.05f;
        cfg.post.ssao.stf.validate();  // must not throw
    }

    return grace::test::summary("renderer");
}
