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

        // Forward frame assembly: pass on default fb, clear, submit, end pass.
        const std::vector<std::string> expected{
            "createGpuMesh", "upload", "setCamera", "beginRenderPass(default)",
            "clear(0,0,0,255)", "submit(lights=0,sun=0,shadow=0,pointShadows=0)", "endRenderPass"};
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
        int defaultPasses = 0;
        for (const std::string& event : backend.events())
            if (event == "beginRenderPass(default)")
                ++defaultPasses;
        GRACE_CHECK_EQ(beginPasses, 1);       // G-buffer pass
        GRACE_CHECK_EQ(defaultPasses, 1);     // lighting pass on the default fb
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

    return grace::test::summary("renderer");
}
