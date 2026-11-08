// Milestone 7, fourth increment: the compose — the light volume adds to the
// frame.
//
// The plan states the proof for this increment: with compose on, the lighting
// pass samples the volume at the fragment's world position and adds albedo *
// radiance to the ambient term (decision 17(d)), so a surface inside the
// volume must get brighter than the same surface with compose off. This test
// does exactly that — one point light inside the volume, one ground quad, the
// centre pixel measured with compose on and off, and both values compared
// against the numbers the documented model says they must be. It does not
// check that the volume looks good; it checks that the pixel is the pixel the
// compose maths says it must be.
//
// WHY THIS NEEDS A GL CONTEXT, measured and not assumed: the compose is a
// shader that samples a 3D texture at the fragment's world position. The null
// backend records events and executes no arithmetic, so it cannot prove the
// numbers. The structural half (the frame order when compose is on, the
// submit record carrying the volume, the rejected configuration) is proved on
// the null backend below.
//
// Both paths are measured: the compose sits in the lighting pass, which is
// shared, but the forward and deferred paths reach it through different code,
// so the same scene is measured in both and the two pixels must agree.
#include "contract_checks.hpp"
#include "grace/render/light.hpp"
#include "grace/render/material.hpp"
#include "grace/render/mesh.hpp"
#include "grace/render/null_render_backend.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/renderer.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "test_support.hpp"

namespace {
using grace::core::vec3;
using grace::render::ClearColor;
using grace::render::ComponentType;
using grace::render::DrawSubmission;
using grace::render::DynamicLight;
using grace::render::GpuMesh;
using grace::render::Mesh;
using grace::render::NullRenderBackend;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::PointLight;
using grace::render::RenderPath;
using grace::render::Renderer;
using grace::render::RendererConfig;
using grace::render::SurfaceParams;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;

/// The chain output for a linear input L at a given exposure: the equations
/// from decisions 15/22 — round(255 * sRGB(aces(L * exposure))).
std::uint8_t chainPixel(float linear, float exposure)
{
    const float x = linear * exposure;
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

bool withinTol(ClearColor actual, ClearColor expected, int tol)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= tol && diff(actual.g, expected.g) <= tol &&
           diff(actual.b, expected.b) <= tol && diff(actual.a, expected.a) <= tol;
}

/// The documented windowed inverse-square attenuation, mirrored from the
/// shader. Written out here on purpose: if the shader's formula changes, this
/// test must change too, which is what makes the comparison a check.
float rangeAtten(float intensity, float range, float d)
{
    if (d > range)
        return 0.0f;
    const float w = 1.0f - d / range;
    return intensity * w * w / (d * d + 1.0f);
}

/// The value the injection writes into the cell whose centre is at
/// (0.5 + x, 0.5 + y, 0.5 + z), for the light at (1, 0.5, 0), intensity 0.5,
/// radius 10, over a 4x4x4 volume spanning [0, 4]^3.
float injectedValue(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    const float dx = (0.5f + static_cast<float>(x)) - 1.0f;
    const float dy = (0.5f + static_cast<float>(y)) - 0.5f;
    const float dz = (0.5f + static_cast<float>(z)) - 0.0f;
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    return rangeAtten(0.5f, 10.0f, d);
}

/// Ground quad z=0 facing +Z, full screen. Camera default: eye (0,0,1), fovY
/// 90 deg, aspect 1.0 — the centre ray is (0,0,1) + t*(0,0,-1), hitting the
/// ground at t = 1.
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
    mesh.setVertexData(std::vector<float>{-1, -1, 0, normal.x, normal.y, normal.z,  //
                                          1, -1, 0, normal.x, normal.y, normal.z,   //
                                          1, 1, 0, normal.x, normal.y, normal.z,    //
                                          -1, 1, 0, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    return mesh;
}

/// The scene config: ambient 0.2 linear, post chain on, SSAO on (flat ground
/// gives occ = 1 at the centre), everything else off. The compose flag is
/// set by the caller. The volume is 4x4x4 with iterations = 0: injection
/// only, so the volume holds pure injected values and the compose's expected
/// pixel is derived exactly from one cell.
RendererConfig baseConfig(RenderPath path)
{
    RendererConfig cfg;
    cfg.path = path;
    cfg.shadows = false;
    cfg.clear_color = ClearColor{0, 0, 0, 255};
    cfg.ambient = 0.2f;  // surface-only centre pixel = chainPixel(0.2) = 149
    cfg.post.enabled = true;
    cfg.post.bloomThreshold = 1000.0f;
    cfg.post.ssao.enabled = true;   // flat ground: occ = 1 at the centre
    cfg.post.mlaa.enabled = false;  // isolate: AA is covered by test_dof_mlaa
    cfg.post.dof.enabled = false;
    cfg.camera.far = 20.0f;
    cfg.volume.enabled = true;
    cfg.volume.resolution = 4;
    cfg.volume.iterations = 0;  // injection only: the volume holds pure
                                // injected values, so the compose's expected
                                // pixel is derived exactly from one cell
    cfg.volume.origin = {0.0f, 0.0f, 0.0f};
    cfg.volume.extent = {4.0f, 4.0f, 4.0f};
    return cfg;
}

template <typename Backend>
std::vector<ClearColor> runFrame(Backend& backend, Renderer& renderer,
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

/// The scene: one point light at (1, 0.5, 0) inside the volume and IN the
/// ground plane, the ground quad at z=0. A light in the ground plane gives
/// ndl = 0 to every fragment of the quad, so the surface gets no direct
/// light from it: the compose OFF pixel is the pure surface value 149, and
/// every pixel difference is the volume's contribution alone.
DrawSubmission groundWithLight(const GpuMesh* mesh)
{
    DrawSubmission sub;
    sub.mesh = mesh;
    SurfaceParams surface;
    surface.albedo = vec3{1.0f, 1.0f, 1.0f};
    sub.material.setSurfaceParams(surface);
    PointLight light;
    light.position = vec3{1.0f, 0.5f, 0.0f};
    light.color = vec3{1.0f, 1.0f, 1.0f};
    light.intensity = 0.5f;
    light.radius = 10.0f;
    sub.lights.push_back(DynamicLight{light});
    return sub;
}

/// The compose's expected pixel, derived: the centre ray hits the ground at
/// world (0, 0, 0), which is inside the volume's cell (0, 0, 0) — the cell
/// whose centre is (0.5, 0.5, 0.5). The injection writes
/// rangeAtten(0.5, 10, 0.7071) = 0.28786 there. With compose on, the lighting
/// pass samples the volume at the fragment's world position and adds albedo *
/// radiance to the ambient term: the centre pixel becomes chainPixel(0.2 +
/// 0.28786) = 205 with the post chain on, 124 legacy (post off), against the
/// surface-only 149 and 51.
const std::uint8_t kExpectedOff = chainPixel(0.2f, 1.0f);
const std::uint8_t kExpectedOn = chainPixel(0.2f + injectedValue(0, 0, 0), 1.0f);

/// OpenGL backend + the scene meshes, created once per test function.
struct Scene
{
    std::unique_ptr<OpenGLRenderBackend> backend;
    std::unique_ptr<GpuMesh> ground;

    explicit Scene(std::uint32_t width = 128, std::uint32_t height = 96)
    {
        OpenGLBackendConfig config;
        config.width = width;
        config.height = height;
        config.hidden = true;
        try
        {
            backend = std::make_unique<OpenGLRenderBackend>(config);
            backend->initialise();
        }
        catch (const std::exception& error)
        {
            std::printf("SKIP: no GL context in this environment: %s\n", error.what());
            std::exit(77);
        }
        ground = backend->createGpuMesh();
        ground->upload(makeGround());
    }
};

/// The compose off must leave the frame unchanged: the surface-only centre
/// pixel, the volume filled but not sampled. The volume IS filled (the
/// injection runs — the frame order inversion does not disable it), but the
/// lighting pass never samples it.
void testComposeOffIsUnchanged(Scene& scene)
{
    RendererConfig cfg = baseConfig(RenderPath::Forward);
    cfg.volume.compose = false;
    Renderer renderer(*scene.backend);
    renderer.setConfig(cfg);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    DrawSubmission sub = groundWithLight(scene.ground.get());
    const ClearColor off = runFrame(*scene.backend, renderer, {sub}, centre)[0];
    std::printf("compose off: centre pixel = %d,%d,%d,%d\n", off.r, off.g, off.b, off.a);
    GRACE_CHECK(withinTol(off, ClearColor{kExpectedOff, kExpectedOff, kExpectedOff, 255}, 2));
    GRACE_CHECK_EQ(scene.backend->glErrorCount(), 0u);
}

/// The compose must add the volume to the frame: with compose on, the centre
/// pixel becomes chainPixel(0.2 + injectedValue(0,0,0)), the surface-only
/// pixel plus the volume's contribution. The compose is the increment's
/// proof: the volume's radiance reaches the frame.
void testComposeAddsVolumeToTheFrame(Scene& scene)
{
    RendererConfig cfg = baseConfig(RenderPath::Forward);
    cfg.volume.compose = true;
    Renderer renderer(*scene.backend);
    renderer.setConfig(cfg);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    DrawSubmission sub = groundWithLight(scene.ground.get());
    const ClearColor on = runFrame(*scene.backend, renderer, {sub}, centre)[0];
    std::printf("compose on: centre pixel = %d,%d,%d,%d\n", on.r, on.g, on.b, on.a);
    GRACE_CHECK(withinTol(on, ClearColor{kExpectedOn, kExpectedOn, kExpectedOn, 255}, 2));
    GRACE_CHECK_EQ(scene.backend->glErrorCount(), 0u);
}

/// The compose must respond to exposure: the volume's radiance is inside the
/// aces argument, so the pixel scales. The exposure 2 pixel is derived the
/// same way the exposure 1 pixel is.
void testComposeRespondsToExposure(Scene& scene)
{
    RendererConfig cfg = baseConfig(RenderPath::Forward);
    cfg.volume.compose = true;
    cfg.post.exposure = 2.0f;
    Renderer renderer(*scene.backend);
    renderer.setConfig(cfg);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    DrawSubmission sub = groundWithLight(scene.ground.get());
    const ClearColor on = runFrame(*scene.backend, renderer, {sub}, centre)[0];
    std::printf("compose on, exposure 2: centre pixel = %d,%d,%d,%d\n", on.r, on.g, on.b, on.a);
    const std::uint8_t expected = chainPixel(0.2f + injectedValue(0, 0, 0), 2.0f);
    GRACE_CHECK(withinTol(on, ClearColor{expected, expected, expected, 255}, 2));
    GRACE_CHECK_EQ(scene.backend->glErrorCount(), 0u);
}

/// The legacy path (post off): with compose off the pixel is the surface-only
/// legacy value 51, with compose on it is the surface value plus the volume's
/// contribution, legacy-clamped. The compose must work in the path the
/// project's compatibility mode keeps.
void testComposeLegacyPath(Scene& scene)
{
    RendererConfig cfg = baseConfig(RenderPath::Forward);
    cfg.post.enabled = false;  // legacy path: linear -> 8-bit clamp
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};

    cfg.volume.compose = false;
    Renderer renderer(*scene.backend);
    renderer.setConfig(cfg);
    DrawSubmission sub = groundWithLight(scene.ground.get());
    const ClearColor off = runFrame(*scene.backend, renderer, {sub}, centre)[0];
    std::printf("compose off, post off: centre pixel = %d,%d,%d,%d\n", off.r, off.g, off.b, off.a);
    const std::uint8_t offLegacy = static_cast<std::uint8_t>(0.2f * 255.0f + 0.5f);
    GRACE_CHECK(withinTol(off, ClearColor{offLegacy, offLegacy, offLegacy, 255}, 2));

    cfg.volume.compose = true;
    Renderer renderer2(*scene.backend);
    renderer2.setConfig(cfg);
    DrawSubmission sub2 = groundWithLight(scene.ground.get());
    const ClearColor on = runFrame(*scene.backend, renderer2, {sub2}, centre)[0];
    std::printf("compose on, post off: centre pixel = %d,%d,%d,%d\n", on.r, on.g, on.b, on.a);
    const float linear = 0.2f + injectedValue(0, 0, 0);
    const std::uint8_t onLegacy = static_cast<std::uint8_t>(linear * 255.0f + 0.5f);
    GRACE_CHECK(withinTol(on, ClearColor{onLegacy, onLegacy, onLegacy, 255}, 2));
    GRACE_CHECK_EQ(scene.backend->glErrorCount(), 0u);
}

/// The deferred path: the compose sits in the deferred lighting pass, which
/// is a different code path from the forward one (the volume is bound to unit
/// 9 in the renderer, not in the backend's submitImpl), so the same scene is
/// measured in both and the two pixels must agree.
void testComposeDeferred(Scene& scene)
{
    RendererConfig cfg = baseConfig(RenderPath::Deferred);
    cfg.volume.compose = true;
    Renderer renderer(*scene.backend);
    renderer.setConfig(cfg);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    DrawSubmission sub = groundWithLight(scene.ground.get());
    const ClearColor on = runFrame(*scene.backend, renderer, {sub}, centre)[0];
    std::printf("compose on (deferred): centre pixel = %d,%d,%d,%d\n", on.r, on.g, on.b, on.a);
    GRACE_CHECK(withinTol(on, ClearColor{kExpectedOn, kExpectedOn, kExpectedOn, 255}, 2));
    GRACE_CHECK_EQ(scene.backend->glErrorCount(), 0u);
}

/// The compose must agree between the paths: the same scene measured in both
/// must give the same pixel, which is what decision 17(d) demands — the LPV
/// indirect light is an ambient term, and the ambient term is the same in
/// both paths.
void testComposeDeferredOff(Scene& scene)
{
    RendererConfig cfg = baseConfig(RenderPath::Deferred);
    cfg.volume.compose = false;
    Renderer renderer(*scene.backend);
    renderer.setConfig(cfg);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    DrawSubmission sub = groundWithLight(scene.ground.get());
    const ClearColor off = runFrame(*scene.backend, renderer, {sub}, centre)[0];
    std::printf("compose off (deferred): centre pixel = %d,%d,%d,%d\n", off.r, off.g, off.b, off.a);
    GRACE_CHECK(withinTol(off, ClearColor{kExpectedOff, kExpectedOff, kExpectedOff, 255}, 2));
    GRACE_CHECK_EQ(scene.backend->glErrorCount(), 0u);
}

/// The compose flag requires the volume to be enabled: validate() rejects a
/// compose flag on a disabled volume, because a compose flag on a disabled
/// volume would silently change nothing.
void testComposeRequiresEnabled()
{
    NullRenderBackend backend(32, 32);
    Renderer renderer(backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;
    config.volume.compose = true;
    bool threw = false;
    try
    {
        renderer.setConfig(config);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
}

/// The compose flag must be a request to change the frame, not a request to
/// fill the volume: with compose on, the lighting pass receives the volume
/// through the submission, so the submit record carries it. The compose flag
/// is a request to change the frame, and the volume must be filled first.
void testComposeSubmitRecord()
{
    // The submit record is a null-backend feature: the OpenGL backend does
    // not keep an event log, so this half runs on the null backend, which
    // records the same events the real backend draws.
    NullRenderBackend nullBackend(64, 48);
    auto mesh = nullBackend.createGpuMesh();
    mesh->upload(grace::test::detail::makeContractTriangle());

    // Compose off: the submit record carries volume=0 — the lighting pass
    // receives no volume.
    RendererConfig cfg = baseConfig(RenderPath::Forward);
    cfg.volume.compose = false;
    Renderer renderer(nullBackend);
    renderer.setConfig(cfg);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    DrawSubmission sub;
    sub.mesh = mesh.get();
    runFrame(nullBackend, renderer, {sub}, centre);
    bool sawVolume = false;
    for (const std::string& event : nullBackend.events())
    {
        if (event.find("submit(") == 0)
            sawVolume = event.find("volume=1") != std::string::npos;
    }
    GRACE_CHECK(!sawVolume);

    // Compose on: the submit record carries volume=1 — the lighting pass
    // receives the volume.
    cfg.volume.compose = true;
    Renderer renderer2(nullBackend);
    renderer2.setConfig(cfg);
    DrawSubmission sub2;
    sub2.mesh = mesh.get();
    runFrame(nullBackend, renderer2, {sub2}, centre);
    sawVolume = false;
    for (const std::string& event : nullBackend.events())
    {
        if (event.find("submit(") == 0)
            sawVolume = event.find("volume=1") != std::string::npos;
    }
    GRACE_CHECK(sawVolume);
}

/// The compose inverts the frame order: with compose on, the volume passes
/// (the injection and the propagation) must run BEFORE the lighting submit,
/// because the lighting pass samples the volume the volume passes filled.
/// The volume passes record their events; the lighting submit records its
/// own; the order must be volume passes first, then the lighting submit.
void testComposeFrameOrder()
{
    // The event log is a null-backend feature: this half runs where the
    // events are recorded.
    NullRenderBackend nullBackend(64, 48);
    auto mesh = nullBackend.createGpuMesh();
    mesh->upload(grace::test::detail::makeContractTriangle());

    RendererConfig cfg = baseConfig(RenderPath::Forward);
    cfg.volume.compose = true;
    Renderer renderer(nullBackend);
    renderer.setConfig(cfg);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    DrawSubmission sub;
    sub.mesh = mesh.get();
    runFrame(nullBackend, renderer, {sub}, centre);
    std::size_t submit = 0;
    std::size_t firstVolume = 0;
    bool sawSubmit = false;
    bool sawVolume = false;
    for (std::size_t i = 0; i < nullBackend.events().size(); ++i)
    {
        if (nullBackend.events()[i].find("submit(") == 0)
        {
            submit = i;
            sawSubmit = true;
        }
        if (nullBackend.events()[i].find("createVolumeLayerTarget") == 0 ||
            nullBackend.events()[i].find("bindTexture3D") == 0)
        {
            if (!sawVolume)
                firstVolume = i;
            sawVolume = true;
        }
    }
    GRACE_CHECK(sawSubmit);
    GRACE_CHECK(sawVolume);
    GRACE_CHECK(firstVolume < submit);
}

}  // namespace

int main()
{
    Scene scene;
    testComposeOffIsUnchanged(scene);
    testComposeAddsVolumeToTheFrame(scene);
    testComposeRespondsToExposure(scene);
    testComposeLegacyPath(scene);
    testComposeDeferred(scene);
    testComposeDeferredOff(scene);
    testComposeRequiresEnabled();
    testComposeSubmitRecord();
    testComposeFrameOrder();
    return grace::test::summary("volume_compose");
}
