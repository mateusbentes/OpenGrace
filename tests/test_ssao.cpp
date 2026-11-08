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
    cfg.post.mlaa.enabled = false;  // isolate: AA is covered by test_dof_mlaa
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

    // A5 base repair (decision 20): the HDR target's depth attachment is the
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

    // SSAO-STF (milestone 6g, decision 31): camera-only reprojection. The
    // scene is static, so a camera move with an unchanged scene keeps the
    // same scene point's world position and the reprojected history is valid
    // — the filtered occlusion is stable within tolerance where the raw
    // buffer's would flicker. The measured subject is the corner pixel
    // (world (0.3, 0.3, 0) on the ground), the same one the corner checks
    // above use.
    {
        RendererConfig cfg = baseConfig(RenderPath::Forward, 1.0f);
        cfg.post.ssao.stf.enabled = true;
        cfg.post.ssao.stf.blend = 0.5f;
        cfg.post.ssao.stf.depthRejection = 0.05f;
        Renderer renderer(*backend);
        renderer.setConfig(cfg);
        const std::vector<DrawSubmission> subs{
            ambientScene(groundMesh.get(), 1.0f),
            ambientScene(wallMesh.get(), 1.0f)};
        // Frame 1: the camera at the default eye (0,0,1). The cold start is
        // the raw buffer, so the corner pixel is the raw value.
        const std::vector<ClearColor> cold =
            runFrame(*backend, renderer, subs, cornerPoints);
        // Frame 2: the camera moved. The same scene point is seen from a
        // different eye, so the raw buffer's occlusion changes, but the
        // reprojection says the same scene point is the same world position —
        // the filtered occlusion is a blend of the raw and the history and
        // the history is the same scene point's occlusion.
        RendererConfig moved = cfg;
        moved.camera.eye = vec3{0.1f, 0.0f, 1.0f};
        renderer.setCamera(moved.camera);
        const std::vector<ClearColor> moved2 =
            runFrame(*backend, renderer, subs, cornerPoints);
        // Frame 3: the camera moved again. The history is two frames old, but
        // the scene point is the same, so the filtered occlusion is stable.
        renderer.setCamera(moved.camera);
        const std::vector<ClearColor> moved3 =
            runFrame(*backend, renderer, subs, cornerPoints);
        std::printf(
            "stf camera move: cold=%d moved=%d movedAgain=%d (raw would flicker)\n",
            static_cast<int>(cold[0].r), static_cast<int>(moved2[0].r),
            static_cast<int>(moved3[0].r));
        // The camera move changes the raw occlusion, but the filtered value
        // is a blend of the raw and the same scene point's history — the
        // change is bounded by the blend weight, so the second frame is
        // within tolerance of the first.
        GRACE_CHECK(withinTol(cold[0], moved2[0], 4));
        GRACE_CHECK(withinTol(moved2[0], moved3[0], 2));
    }

    // SSAO-STF rejection rule: a pixel whose reprojected history is valid is
    // blended; a pixel whose history is rejected gets raw. A LARGE blend (0.9)
    // makes the two cases distinguishable numerically: if the reprojected uv
    // for this pixel lands on BACKGROUND in the previous frame — here the
    // camera pulls back far enough that part of the ground leaves the frame —
    // the history lookup reads a background texel whose quantised depth was
    // recorded as 1.0, disagreeing with this pixel's depth by more than
    // depthRejection, and the history is rejected. The measured subject is an
    // edge pixel: one inside the scene at both cameras (history accepted,
    // blended) and one outside in the PREVIOUS frame only when reprojection
    // misses — covered by measuring the corner pixel across a large pull-back
    // where its reprojected position falls outside [0,1].
    {
        RendererConfig cfg = baseConfig(RenderPath::Forward, 1.0f);
        cfg.post.ssao.stf.enabled = true;
        cfg.post.ssao.stf.blend = 0.9f;  // amplifies any ghosting to ~10% of the delta
        Renderer renderer(*backend);
        renderer.setConfig(cfg);
        const std::vector<DrawSubmission> subs{
            ambientScene(groundMesh.get(), 1.0f),
            ambientScene(wallMesh.get(), 1.0f)};
        const std::vector<ClearColor> cold =
            runFrame(*backend, renderer, subs, cornerPoints);
        // The camera moves far enough that the corner pixel's world position
        // is reprojected off-screen: the history is invalid, so the raw
        // occlusion is the output.
        RendererConfig pulled = cfg;
        pulled.camera.eye = vec3{0.0f, 0.0f, 2.0f};  // ground fills less of the frame now
        renderer.setCamera(pulled.camera);
        const std::vector<ClearColor> offScreen =
            runFrame(*backend, renderer, subs, cornerPoints);
        // The same scene at the same camera with the filter off: the raw
        // buffer the tonemap samples when STF is off.
        RendererConfig rawCfg = baseConfig(RenderPath::Forward, 1.0f);
        Renderer rawRenderer(*backend);
        rawRenderer.setConfig(rawCfg);
        rawRenderer.setCamera(pulled.camera);
        const std::vector<ClearColor> raw =
            runFrame(*backend, rawRenderer, subs, cornerPoints);
        std::printf("stf disocclusion: cold=%d offScreen=%d raw=%d (history rejected -> raw)\n",
                    static_cast<int>(cold[0].r), static_cast<int>(offScreen[0].r),
                    static_cast<int>(raw[0].r));
        GRACE_CHECK(withinTol(offScreen[0], raw[0], 1));
    }

    // SSAO-STF off by default: the config default is off, so the config
    // does not change the frame — bit-identical to before the filter
    // existed (the same rule the post chain follows).
    {
        RendererConfig cfg = baseConfig(RenderPath::Forward, 1.0f);
        Renderer renderer(*backend);
        renderer.setConfig(cfg);
        const std::vector<DrawSubmission> subs{
            ambientScene(groundMesh.get(), 1.0f),
            ambientScene(wallMesh.get(), 1.0f)};
        const std::vector<ClearColor> without =
            runFrame(*backend, renderer, subs, cornerPoints);
        RendererConfig onCfg = baseConfig(RenderPath::Forward, 1.0f);
        onCfg.post.ssao.stf.enabled = true;
        Renderer onRenderer(*backend);
        onRenderer.setConfig(onCfg);
        const std::vector<ClearColor> with =
            runFrame(*backend, onRenderer, subs, cornerPoints);
        GRACE_CHECK(withinTol(with[0], without[0], 1));
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("ssao");
}
