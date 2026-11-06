#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/renderer.hpp"

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

// Runs one frame and captures the queued pixels (queued before endFrame:
// swapBuffers captures them pre-swap, decision 16).
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

// Axis-aligned quad in the z = z plane facing +Z. Default camera: eye (0,0,1),
// fovY 90 deg, aspect 1.0, so a world point (x, y, z) lands on pixel
// ((x/(1-z)+1)*64, (y/(1-z)+1)*48) of the 128x96 frame.
Mesh makeQuad(float z, float x0, float y0, float x1, float y1)
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
    mesh.setVertexData(std::vector<float>{x0, y0, z, normal.x, normal.y, normal.z,  //
                                          x1, y0, z, normal.x, normal.y, normal.z,  //
                                          x1, y1, z, normal.x, normal.y, normal.z,  //
                                          x0, y1, z, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
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

    // DOF scene: the ground split at x=0 into a dark half (albedo 0.05) and a
    // bright half (albedo 1), lit by a sun of 0.5 + ambient 0.2 -> hard edge
    // at pixel column 64: dark 44, bright 220 (measured). DOF focus 1.0 = the
    // ground plane (sharp); focus 0.5 blurs the ground with CoC clamped at 12 px.
    auto leftGround = backend->createGpuMesh();
    leftGround->upload(makeQuad(0.0f, -1.0f, -1.0f, 0.0f, 1.0f));
    auto rightGround = backend->createGpuMesh();
    rightGround->upload(makeQuad(0.0f, 0.0f, -1.0f, 1.0f, 1.0f));

    // MLAA scene: same split but ambient-only (no sun): black half reads 0,
    // white half reads 149 (0.2 linear through the chain).
    auto runDofScene = [&](Renderer& renderer, bool withSun,
                           const std::vector<std::pair<std::uint32_t, std::uint32_t>>& row) {
        DrawSubmission left;
        left.mesh = leftGround.get();
        SurfaceParams ls;
        ls.albedo = vec3{0.05f, 0.05f, 0.05f};
        left.material.setSurfaceParams(ls);
        DrawSubmission right;
        right.mesh = rightGround.get();
        SurfaceParams rs;
        rs.albedo = vec3{1.0f, 1.0f, 1.0f};
        right.material.setSurfaceParams(rs);
        if (withSun)
        {
            DirectionalLight sun;
            sun.direction = vec3{0.0f, 0.0f, -1.0f};
            sun.color = vec3{0.5f, 0.5f, 0.5f};
            left.sun = sun;
            right.sun = sun;
        }
        return runFrame(*backend, renderer, {left, right}, row);
    };

    // A5: depth of field, known circle of confusion. Row y=48, columns given
    // above; index 0 = x64 (edge), 6 = x52 (12 px left), 7 = x50 (14 px left).
    {
        RendererConfig cfgOff = baseConfig(RenderPath::Forward, 0.2f);  // dof off
        Renderer rOff(*backend);
        rOff.setConfig(cfgOff);
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> dofRow{
            {64, 48}, {62, 48}, {60, 48}, {58, 48}, {56, 48}, {54, 48},
            {52, 48}, {50, 48}, {66, 48}, {68, 48}, {72, 48}, {76, 48}};
        const auto off = runDofScene(rOff, true, dofRow);

        RendererConfig cfgSharp = cfgOff;
        cfgSharp.post.dof.enabled = true;
        cfgSharp.post.dof.focusDistance = 1.0f;  // the ground plane: sharp
        Renderer rSharp(*backend);
        rSharp.setConfig(cfgSharp);
        const auto sharp = runDofScene(rSharp, true, dofRow);

        RendererConfig cfgBlur = cfgOff;
        cfgBlur.post.dof.enabled = true;
        cfgBlur.post.dof.focusDistance = 0.5f;  // in front of the ground: CoC clamped 12 px
        cfgBlur.post.dof.aperture = 0.5f;
        cfgBlur.post.dof.maxCocPixels = 12.0f;
        Renderer rBlur(*backend);
        rBlur.setConfig(cfgBlur);
        const auto blur = runDofScene(rBlur, true, dofRow);

        std::printf("DOF edge row (x=64,62,60,58,56,54,52,50,66,68,72,76):\n");
        std::printf("  off:        ");
        for (const auto& c : off) std::printf(" %3d", static_cast<int>(c.r));
        std::printf("\n  focus=1.0:  ");
        for (const auto& c : sharp) std::printf(" %3d", static_cast<int>(c.r));
        std::printf("\n  focus=0.5:  ");
        for (const auto& c : blur) std::printf(" %3d", static_cast<int>(c.r));
        std::printf("\n");

        // Hard edge without DOF: dark side 44, bright side 220, step at x=64.
        GRACE_CHECK(withinTol(off[1], ClearColor{44, 44, 44, 255}, 3));   // x=62 dark
        GRACE_CHECK(withinTol(off[8], ClearColor{220, 220, 220, 255}, 3)); // x=66 bright
        // Focus on the plane (1.0): bit-identical to off (sharp plane).
        for (std::size_t i = 0; i < off.size(); ++i)
            GRACE_CHECK(withinTol(sharp[i], off[i], 1));
        // Focus 0.5: CoC = 0.5*|1.0-0.5|/1.0 / (2*tan(45)/96) = 12 px (clamped).
        // The edge pixel blends (220 -> 195), a pixel 10 px out blends
        // (44 -> 82), a pixel 14 px out is untouched (44) — the disc cutoff.
        GRACE_CHECK(blur[0].r < off[0].r - 10);                 // centre blended down
        GRACE_CHECK(blur[5].r > off[5].r + 20);                 // x=54 blended up
        GRACE_CHECK(withinTol(blur[7], off[7], 2));             // x=50 outside the disc
        GRACE_CHECK(withinTol(blur[11], off[11], 2));           // x=76 outside the disc
        // Bokeh disc, not a Gaussian: the blend is strong at 10 px and zero at
        // 14 px (sharp cutoff at the 12 px CoC), not a gradual decay.
        GRACE_CHECK(blur[5].r - off[5].r > 20 && withinTol(blur[7], off[7], 2));

        // A4/A5: both paths agree with DOF on (same depth in both, decision 19).
        RendererConfig cfgBlurDef = cfgBlur;
        cfgBlurDef.path = RenderPath::Deferred;
        Renderer rBlurDef(*backend);
        rBlurDef.setConfig(cfgBlurDef);
        const auto blurDef = runDofScene(rBlurDef, true, dofRow);
        std::printf("  deferred:   ");
        for (const auto& c : blurDef) std::printf(" %3d", static_cast<int>(c.r));
        std::printf("\n");
        for (std::size_t i = 0; i < blur.size(); ++i)
            GRACE_CHECK(withinTol(blur[i], blurDef[i], 3));
    }

    // A5: morphological AA on a synthetic high-contrast edge. Ambient-only:
    // black half 0, white half 149, hard step at x=64.
    {
        RendererConfig cfgOff = baseConfig(RenderPath::Forward, 0.2f);  // mlaa off
        Renderer rOff(*backend);
        rOff.setConfig(cfgOff);
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> mlaaRow{
            {64, 48}, {63, 48}, {62, 48}, {65, 48}, {66, 48}, {56, 48}, {72, 48}};
        const auto off = runDofScene(rOff, false, mlaaRow);

        RendererConfig cfgOn = cfgOff;
        cfgOn.post.mlaa.enabled = true;
        Renderer rOn(*backend);
        rOn.setConfig(cfgOn);
        const auto on = runDofScene(rOn, false, mlaaRow);

        std::printf("MLAA edge row (x=64,63,62,65,66,56,72):\n");
        std::printf("  off: ");
        for (const auto& c : off) std::printf(" %3d", static_cast<int>(c.r));
        std::printf("\n  on:  ");
        for (const auto& c : on) std::printf(" %3d", static_cast<int>(c.r));
        std::printf("\n");

        // The MLAA row samples x=64,63,62,65,66,56,72 (indices 0..6). Hard step
        // off: dark side 12 (albedo 0.05 * ambient 0.2), bright side 149.
        GRACE_CHECK(withinTol(off[2], ClearColor{12, 12, 12, 255}, 3));    // x=62 dark
        GRACE_CHECK(withinTol(off[6], ClearColor{149, 149, 149, 255}, 3)); // x=72 bright
        // On: the two pixels straddling the edge (x=63, x=64) are blended
        // strictly between (12/149 -> ~103), the interior pixels are unchanged
        // — a single-pixel edge is smoothed.
        GRACE_CHECK(on[0].r > 40 && on[0].r < 130);   // x=64 blended (~103)
        GRACE_CHECK(on[1].r > 40 && on[1].r < 130);   // x=63 blended (~103)
        GRACE_CHECK(withinTol(on[5], off[5], 2));     // x=56 unchanged
        GRACE_CHECK(withinTol(on[6], off[6], 2));     // x=72 unchanged
        GRACE_CHECK(on[0].r != off[0].r && on[1].r != off[1].r);
    }

    // A4: both techniques off leaves the pixels of the current tree unchanged
    // (the SSAO scene of test_ssao: flat ambient-1.0 surface reads 232).
    {
        auto flat = backend->createGpuMesh();
        flat->upload(makeQuad(0.0f, -1.0f, -1.0f, 1.0f, 1.0f));
        RendererConfig cfg = baseConfig(RenderPath::Forward, 1.0f);  // ssao on, mlaa/dof off
        Renderer renderer(*backend);
        renderer.setConfig(cfg);
        DrawSubmission sub;
        sub.mesh = flat.get();
        SurfaceParams surface;
        surface.albedo = vec3{1.0f, 1.0f, 1.0f};
        sub.material.setSurfaceParams(surface);
        sub.ambient = 1.0f;
        const auto pixels = runFrame(*backend, renderer, {sub}, {{64, 48}});
        std::printf("both techniques off, flat ambient-1.0 surface: pixel=%d (current tree: 232)\n",
                    static_cast<int>(pixels[0].r));
        GRACE_CHECK(withinTol(pixels[0], ClearColor{232, 232, 232, 255}, 1));
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("dof_mlaa");
}
