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
#include <utility>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::vec3;
using grace::render::ClearColor;
using grace::render::ComponentType;
using grace::render::DirectionalLight;
using grace::render::DrawSubmission;
using grace::render::GpuMesh;
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

// Runs one frame with the given submissions and captures the queued pixels
// (queued before endFrame: swapBuffers captures them pre-swap, decision 16).
std::vector<ClearColor> runFrame(OpenGLRenderBackend& backend, Renderer& renderer,
                                 const std::vector<DrawSubmission>& subs,
                                 const std::vector<std::pair<std::uint32_t, std::uint32_t>>& points)
{
    renderer.beginFrame();
    for (const DrawSubmission& sub : subs)
        renderer.submit(sub);
    for (const auto& point : points)
        backend.queueReadback(point.first, point.second);
    renderer.endFrame();
    std::vector<ClearColor> out;
    for (const auto& sample : backend.lastReadback())
        out.push_back(sample.color);
    return out;
}

// Flat ground quad x,y in [-1,1] in the z=0 plane facing +Z. Default camera:
// eye (0,0,1), fovY 90 deg, aspect 1.0, so a world point (x, y, 0) lands on
// pixel ((x+1)*64, (y+1)*48) of the 128x96 frame.
Mesh makeGround()
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
        -1.0f, -1.0f, 0.0f, normal.x, normal.y, normal.z,  //
        1.0f, -1.0f, 0.0f, normal.x, normal.y, normal.z,   //
        1.0f, 1.0f, 0.0f, normal.x, normal.y, normal.z,    //
        -1.0f, 1.0f, 0.0f, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    return mesh;
}

// Two walls standing on the ground at x = 0.35 (facing -X) and y = 0.35
// (facing -Y), forming a concave corner with the ground. Both are well inside
// the camera frustum and far from edge-on, so they rasterize with real screen
// area and appear in the depth buffer. Ground points near (0.3, 0.3) are
// occluded by both.
Mesh makeCornerWalls()
{
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 6 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    layout.attributes.push_back(VertexAttribute{VertexSemantic::Normal, ComponentType::Float32,
                                                3, 3 * sizeof(float), false});
    mesh.setLayout(layout);
    const vec3 nSide{-1.0f, 0.0f, 0.0f};
    const vec3 nBack{0.0f, -1.0f, 0.0f};
    mesh.setVertexData(std::vector<float>{
        // side wall: x = 0.35, y in [-1,1], z in [0,0.8]
        0.35f, -1.0f, 0.0f, nSide.x, nSide.y, nSide.z,  //
        0.35f, 1.0f, 0.0f, nSide.x, nSide.y, nSide.z,   //
        0.35f, 1.0f, 0.8f, nSide.x, nSide.y, nSide.z,   //
        0.35f, -1.0f, 0.8f, nSide.x, nSide.y, nSide.z,  //
        // back wall: y = 0.35, x in [-1,1], z in [0,0.8]
        -1.0f, 0.35f, 0.0f, nBack.x, nBack.y, nBack.z,  //
        1.0f, 0.35f, 0.0f, nBack.x, nBack.y, nBack.z,   //
        1.0f, 0.35f, 0.8f, nBack.x, nBack.y, nBack.z,   //
        -1.0f, 0.35f, 0.8f, nBack.x, nBack.y, nBack.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7});
    return mesh;
}

RendererConfig baseConfig(RenderPath path, float ambient)
{
    RendererConfig cfg;
    cfg.path = path;
    cfg.shadows = false;
    cfg.clear_color = ClearColor{0, 0, 0, 255};
    cfg.ambient = ambient;
    cfg.post.enabled = true;
    cfg.post.bloomThreshold = 1000.0f;  // bloom contributes nothing in these checks
    cfg.post.ssao.enabled = true;
    cfg.camera.far = 20.0f;
    return cfg;
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
    groundMesh->upload(makeGround());
    auto wallMesh = backend->createGpuMesh();
    wallMesh->upload(makeCornerWalls());

    // Ambient-only submission (white albedo, ambient set from config by the
    // renderer). Ambient 1.0 makes occlusion measurable in 8-bit output: one
    // occlusion step (1/8 of the samples) moves the pixel by ~4/255, while at
    // the default ambient 0.05 the same step is only ~2/255.
    auto ambientScene = [&](const GpuMesh* mesh, float ambient) {
        DrawSubmission sub;
        sub.mesh = mesh;
        SurfaceParams surface;
        surface.albedo = vec3{1.0f, 1.0f, 1.0f};
        sub.material.setSurfaceParams(surface);
        sub.ambient = ambient;
        return sub;
    };

    const std::vector<std::pair<std::uint32_t, std::uint32_t>> center{{64, 48}};
    // Near-corner ground point: world (0.3, 0.3, 0) -> pixel (83, 62). Open
    // ground point: world (-0.6, -0.6, 0) -> pixel (26, 19).
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> cornerPoints{{83, 62}, {26, 19}};

    // A5: a flat surface is not darkened by its own geometry (normal bias
    // prevents self-occlusion), and with SSAO off the result is identical.
    {
        Renderer onRenderer(*backend);
        onRenderer.setConfig(baseConfig(RenderPath::Forward, 1.0f));
        const ClearColor withSSAO =
            runFrame(*backend, onRenderer, {ambientScene(groundMesh.get(), 1.0f)}, center)[0];

        RendererConfig offCfg = baseConfig(RenderPath::Forward, 1.0f);
        offCfg.post.ssao.enabled = false;
        Renderer offRenderer(*backend);
        offRenderer.setConfig(offCfg);
        const ClearColor noSSAO =
            runFrame(*backend, offRenderer, {ambientScene(groundMesh.get(), 1.0f)}, center)[0];

        std::printf("flat surface: ssao on=%d  ssao off=%d (must be equal; unoccluded is 232)\n",
                    static_cast<int>(withSSAO.r), static_cast<int>(noSSAO.r));
        GRACE_CHECK(withinTol(withSSAO, noSSAO, 1));
        GRACE_CHECK(withSSAO.r > 230);  // genuinely unoccluded, not just equal
    }

    // A5: a concave corner (ground + side wall) is darkened next to the wall,
    // with real values; the open area of the same surface is not.
    {
        Renderer renderer(*backend);
        renderer.setConfig(baseConfig(RenderPath::Forward, 1.0f));
        const auto pixels = runFrame(*backend, renderer,
                                     {ambientScene(groundMesh.get(), 1.0f),
                                      ambientScene(wallMesh.get(), 1.0f)},
                                     cornerPoints);
        const ClearColor corner = pixels[0];
        const ClearColor open = pixels[1];
        std::printf("corner: near-wall pixel=%d  open-area pixel=%d (near-wall must be darker)\n",
                    static_cast<int>(corner.r), static_cast<int>(open.r));
        GRACE_CHECK(corner.r < open.r);
        GRACE_CHECK(corner.r < 232);  // darker than the unoccluded flat value
        GRACE_CHECK(open.r > 230);    // open area stays unoccluded
    }

    // A4/A5: both paths produce the same occlusion for the same scene (the SSAO
    // pass reads the scene depth: HDR depth in forward, G-buffer depth in
    // deferred).
    {
        Renderer fwd(*backend);
        fwd.setConfig(baseConfig(RenderPath::Forward, 1.0f));
        const ClearColor f = runFrame(*backend, fwd,
                                      {ambientScene(groundMesh.get(), 1.0f),
                                       ambientScene(wallMesh.get(), 1.0f)},
                                      cornerPoints)[0];

        Renderer def(*backend);
        def.setConfig(baseConfig(RenderPath::Deferred, 1.0f));
        const ClearColor d = runFrame(*backend, def,
                                      {ambientScene(groundMesh.get(), 1.0f),
                                       ambientScene(wallMesh.get(), 1.0f)},
                                      cornerPoints)[0];
        std::printf("path agreement on corner: forward=%d  deferred=%d  diff=%d\n",
                    static_cast<int>(f.r), static_cast<int>(d.r),
                    static_cast<int>(f.r) - static_cast<int>(d.r));
        GRACE_CHECK(withinTol(f, d, 1));
        GRACE_CHECK(f.r < 232);  // deferred corner is darkened too
    }

    // A5: the ambient term is what is darkened — a direct-light-only scene
    // (ambient 0) is unaffected by SSAO.
    {
        auto litScene = [&]() {
            DrawSubmission sub = ambientScene(groundMesh.get(), 0.0f);
            DirectionalLight sun;
            sun.direction = vec3{0.0f, 0.0f, -1.0f};
            sun.color = vec3{0.5f, 0.5f, 0.5f};
            sub.sun = sun;
            return sub;
        };

        Renderer onRenderer(*backend);
        onRenderer.setConfig(baseConfig(RenderPath::Forward, 0.0f));
        const ClearColor a = runFrame(*backend, onRenderer, {litScene()}, center)[0];

        RendererConfig offCfg = baseConfig(RenderPath::Forward, 0.0f);
        offCfg.post.ssao.enabled = false;
        Renderer offRenderer(*backend);
        offRenderer.setConfig(offCfg);
        const ClearColor b = runFrame(*backend, offRenderer, {litScene()}, center)[0];

        std::printf("direct-only scene: ssao on=%d  ssao off=%d (must be equal)\n",
                    static_cast<int>(a.r), static_cast<int>(b.r));
        GRACE_CHECK(withinTol(a, b, 1));
    }

    // A5 base repair (decision 19): the HDR target's depth attachment is the
    // scene depth in BOTH paths — read it back after a full frame and compare.
    // Before the repair the deferred path's fullscreen lighting quad overwrote
    // it with 0.5 (the quad's window depth); now it must equal the forward
    // path's scene depth, and neither may be 0.5.
    {
        auto hdrDepthAt = [&](Renderer& renderer) {
            backend->beginRenderPass(renderer.hdrTarget());
            backend->queueReadback(64, 48);
            backend->readback();
            const float depth = backend->lastReadback()[0].depth;
            backend->endRenderPass();
            return depth;
        };

        Renderer fwd(*backend);
        fwd.setConfig(baseConfig(RenderPath::Forward, 1.0f));
        runFrame(*backend, fwd, {ambientScene(groundMesh.get(), 1.0f)}, center);
        const float fwdDepth = hdrDepthAt(fwd);

        Renderer def(*backend);
        def.setConfig(baseConfig(RenderPath::Deferred, 1.0f));
        runFrame(*backend, def, {ambientScene(groundMesh.get(), 1.0f)}, center);
        const float defDepth = hdrDepthAt(def);

        std::printf("HDR depth after frame: forward=%.4f  deferred=%.4f (quad depth would be 0.5)\n",
                    fwdDepth, defDepth);
        GRACE_CHECK(std::fabs(fwdDepth - defDepth) < 0.001f);
        GRACE_CHECK(std::fabs(fwdDepth - 0.5f) > 0.001f);  // not the fullscreen quad's depth
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("ssao");
}
