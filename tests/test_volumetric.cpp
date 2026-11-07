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
using grace::render::DrawSubmission;
using grace::render::GpuMesh;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::PointLight;
using grace::render::RenderPath;
using grace::render::Renderer;
using grace::render::RendererConfig;
using grace::render::SpotLight;
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

// The chain output for a linear input L at a given exposure: the equations
// from decisions 15/22 — round(255 * sRGB(aces(L * exposure))).
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

// The documented windowed inverse-square attenuation, mirrored from the shader.
float rangeAtten(float intensity, float range, float d)
{
    if (d > range)
        return 0.0f;
    const float w = 1.0f - d / range;
    return intensity * w * w / (d * d + 1.0f);
}

// Mirror of the volume shader's march for the centre ray: o = (0,0,1),
// dir = (0,0,-1), surface at distance rayDist. Sphere volume -> analytic
// ray-sphere segment; the camera-inside case clamps t0 to 0 exactly like the
// shader. Returns the linear in-scattered radiance v (isotropic phase 1/4pi).
float sphereVolumeRadiance(vec3 lightPos, float radius, float intensity, float density,
                           float rayDist)
{
    const vec3 o{0.0f, 0.0f, 1.0f};
    const vec3 dir{0.0f, 0.0f, -1.0f};
    const vec3 oc{o.x - lightPos.x, o.y - lightPos.y, o.z - lightPos.z};
    const float b = oc.x * dir.x + oc.y * dir.y + oc.z * dir.z;
    const float cc = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - radius * radius;
    const float disc = b * b - cc;
    if (disc < 0.0f)
        return 0.0f;
    const float s = std::sqrt(disc);
    const float tr0 = -b - s;
    const float tr1 = -b + s;
    if (tr1 <= 0.0f)
        return 0.0f;
    const float t0 = std::fmax(tr0, 0.0f);  // camera inside: clamp entry to 0
    const float t1 = std::fmin(tr1, rayDist);
    if (t1 <= t0)
        return 0.0f;
    const float ds = (t1 - t0) / 16.0f;
    float sum = 0.0f;
    for (int k = 0; k < 16; ++k)
    {
        const float t = t0 + (static_cast<float>(k) + 0.5f) * ds;
        const float dist = std::fabs(t - (1.0f - lightPos.z));  // |p - light| on the centre ray
        if (dist > radius)
            continue;
        sum += rangeAtten(intensity, radius, dist) * ds;  // shadow = 1 (no map)
    }
    return intensity == 0.0f ? 0.0f : density * sum / (4.0f * 3.14159265358979f);
}

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

// Ground quad z=0 facing +Z, full screen. Camera default: eye (0,0,1), fovY
// 90 deg, aspect 1.0 — the centre ray is (0,0,1) + t*(0,0,-1), hitting the
// ground at t = 1.
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
                                          1, -1, 0, normal.x, normal.y, normal.z,    //
                                          1, 1, 0, normal.x, normal.y, normal.z,     //
                                          -1, 1, 0, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    return mesh;
}

Mesh makeOccluder()
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
    mesh.setVertexData(std::vector<float>{-0.5f, -0.5f, 0.55f, normal.x, normal.y, normal.z,  //
                                          0.5f, -0.5f, 0.55f, normal.x, normal.y, normal.z,   //
                                          0.5f, 0.5f, 0.55f, normal.x, normal.y, normal.z,    //
                                          -0.5f, 0.5f, 0.55f, normal.x, normal.y, normal.z});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    return mesh;
}

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

    auto ground = backend->createGpuMesh();
    ground->upload(makeGround());
    auto occluder = backend->createGpuMesh();
    occluder->upload(makeOccluder());

    // Ambient-only ground, albedo 1: the centre surface is exactly 0.2 linear
    // -> 149 through the chain, so any volume addition is auditable as
    // chainPixel(0.2 + v) with v derived below from the documented model.
    auto groundSub = [&] {
        DrawSubmission sub;
        sub.mesh = ground.get();
        SurfaceParams surface;
        surface.albedo = vec3{1.0f, 1.0f, 1.0f};
        sub.material.setSurfaceParams(surface);
        return sub;
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centre{{64, 48}};
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> centreAndEdge{{64, 48}, {112, 48}};

    // A2: a point-style volume (sphere) scatters along the ray; the pixel is
    // the derived chainPixel(0.2 + v), and it responds to exposure.
    {
        PointLight light;
        light.position = vec3{0.0f, 0.0f, 0.5f};
        light.color = vec3{1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;
        light.radius = 0.4f;
        light.volumeDensity = 2.0f;

        const float v = sphereVolumeRadiance(light.position, light.radius, light.intensity,
                                              light.volumeDensity, 1.0f);
        const std::uint8_t expected = chainPixel(0.2f + v, 1.0f);
        const std::uint8_t expectedExp2 = chainPixel(0.2f + v, 2.0f);
        std::printf("sphere volume: derived v=%.5f -> expected pixel %d (exposure 2: %d)\n",
                    static_cast<double>(v), static_cast<int>(expected),
                    static_cast<int>(expectedExp2));

        RendererConfig cfg = baseConfig(RenderPath::Forward);
        DrawSubmission sub = groundSub();
        sub.lights.push_back(light);
        Renderer renderer(*backend);
        renderer.setConfig(cfg);
        const ClearColor withVol = runFrame(*backend, renderer, {sub}, centre)[0];

        // Same frame without the volume (density 0): exactly the surface value.
        DrawSubmission plain = groundSub();
        PointLight off = light;
        off.volumeDensity = 0.0f;
        plain.lights.push_back(off);
        const ClearColor withoutVol = runFrame(*backend, renderer, {plain}, centre)[0];

        // Exposure 2: the volume is inside the aces argument, so it scales.
        RendererConfig cfg2 = cfg;
        cfg2.post.exposure = 2.0f;
        Renderer renderer2(*backend);
        renderer2.setConfig(cfg2);
        const ClearColor exp2 = runFrame(*backend, renderer2, {sub}, centre)[0];

        std::printf("centre: no volume=%d  with volume=%d  exposure2=%d\n",
                    static_cast<int>(withoutVol.r), static_cast<int>(withVol.r),
                    static_cast<int>(exp2.r));
        GRACE_CHECK(withinTol(withoutVol, ClearColor{149, 149, 149, 255}, 2));
        GRACE_CHECK(withVol.r > withoutVol.r + 5);  // the shaft is visible
        GRACE_CHECK(withinTol(withVol, ClearColor{expected, expected, expected, 255}, 3));
        GRACE_CHECK(withinTol(exp2, ClearColor{expectedExp2, expectedExp2, expectedExp2, 255}, 3));
    }

    // A2: camera INSIDE the volume — the entry t is clamped to 0, the march
    // never runs backwards, and the pixel matches the derived value.
    {
        PointLight light;
        light.position = vec3{0.0f, 0.0f, 1.0f};  // exactly at the camera
        light.color = vec3{1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;
        light.radius = 0.5f;
        light.volumeDensity = 2.0f;

        const float v = sphereVolumeRadiance(light.position, light.radius, light.intensity,
                                              light.volumeDensity, 1.0f);
        const std::uint8_t expected = chainPixel(0.2f + v, 1.0f);
        std::printf("camera inside sphere: derived v=%.5f -> expected pixel %d\n",
                    static_cast<double>(v), static_cast<int>(expected));

        Renderer renderer(*backend);
        renderer.setConfig(baseConfig(RenderPath::Forward));
        DrawSubmission sub = groundSub();
        sub.lights.push_back(light);
        const ClearColor p = runFrame(*backend, renderer, {sub}, centre)[0];
        std::printf("camera inside: centre pixel=%d (surface only 149)\n", static_cast<int>(p.r));
        GRACE_CHECK(p.r > 149 + 5);
        GRACE_CHECK(withinTol(p, ClearColor{expected, expected, expected, 255}, 3));
    }

    // A2: a spot-style volume is a cone — full in-scattering on the axis,
    // none outside the outer cone.
    {
        SpotLight light;
        light.position = vec3{0.0f, 0.0f, 0.8f};
        light.direction = vec3{0.0f, 0.0f, -1.0f};
        light.color = vec3{1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;
        light.range = 1.0f;
        light.innerAngle = 0.3f;
        light.outerAngle = 0.6f;
        light.volumeDensity = 2.0f;

        // Derive v for the centre ray by mirroring the shader's cone march.
        const vec3 axis = vec3{0.0f, 0.0f, -1.0f};
        const float outerCos = std::cos(light.outerAngle);
        const float innerCos = std::cos(light.innerAngle);
        float sum = 0.0f;
        const float ds = 1.0f / 16.0f;
        for (int k = 0; k < 16; ++k)
        {
            const float t = (static_cast<float>(k) + 0.5f) * ds;
            const vec3 p{0.0f, 0.0f, 1.0f - t};
            const vec3 toL = light.position - p;
            const float dist = length(toL);
            const vec3 L = toL / std::fmax(dist, 1e-6f);
            const float c = -(L.x * axis.x + L.y * axis.y + L.z * axis.z);
            float cone = 0.0f;
            if (c <= innerCos && c >= outerCos)
                cone = (c - outerCos) / (innerCos - outerCos);
            else if (c > innerCos)
                cone = 1.0f;
            cone = cone * cone * (3.0f - 2.0f * cone);  // smoothstep
            if (cone <= 0.0f)
                continue;
            sum += rangeAtten(light.intensity, light.range, dist) * cone * ds;
        }
        const float v = light.volumeDensity * sum / (4.0f * 3.14159265358979f);
        // The spot also shades the ground surface at the centre (on-axis, ndl
        // = 1): surface light = rangeAtten(1, range, 0.8) * cone(1) = 0.024390.
        const float surfaceLight = rangeAtten(light.intensity, light.range, 0.8f);
        const std::uint8_t expected = chainPixel(0.2f + surfaceLight + v, 1.0f);
        std::printf("cone volume: derived v=%.5f surface=%.5f -> expected centre pixel %d\n",
                    static_cast<double>(v), static_cast<double>(surfaceLight),
                    static_cast<int>(expected));

        Renderer renderer(*backend);
        renderer.setConfig(baseConfig(RenderPath::Forward));
        DrawSubmission sub = groundSub();
        sub.lights.push_back(light);
        const auto pixels = runFrame(*backend, renderer, {sub}, centreAndEdge);
        std::printf("cone: centre=%d  outside-outer-cone(x=112)=%d (surface only 149)\n",
                    static_cast<int>(pixels[0].r), static_cast<int>(pixels[1].r));
        GRACE_CHECK(withinTol(pixels[0], ClearColor{expected, expected, expected, 255}, 3));
        GRACE_CHECK(withinTol(pixels[1], ClearColor{149, 149, 149, 255}, 2));
    }

    // A3: a shadowed point-style volume is darker along the ray than an
    // unshadowed one (occluder between the light and part of the ray).
    {
        RendererConfig cfg = baseConfig(RenderPath::Forward);
        cfg.shadows = true;
        cfg.pointShadows.mapSize = 256;
        cfg.pointShadows.near = 0.02f;  // the occluder sits 0.05 from the light: inside near..far

        // Light at z=0.5; the occluder at z=0.65 sits between the light and
        // the camera (0.15 from the light, inside the map's near..far), so it
        // shadows the ray samples on the camera side of it — the bright ones
        // near the light.
        PointLight light;
        light.position = vec3{0.0f, 0.0f, 0.5f};
        light.color = vec3{1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;
        light.radius = 0.4f;
        light.volumeDensity = 6.0f;  // strong shaft so the shadowed drop is several levels

        Renderer renderer(*backend);
        renderer.setConfig(cfg);
        DrawSubmission lit = groundSub();
        lit.lights.push_back(light);
        lit.shadowLightRequests = 1;  // this light gets a paraboloid shadow map
        const ClearColor unshadowed = runFrame(*backend, renderer, {lit}, centre)[0];

        DrawSubmission occ = groundSub();
        occ.mesh = occluder.get();
        occ.castOnly = true;  // casts into the shadow map, is not drawn
        DrawSubmission litOcc = lit;
        const ClearColor shadowed =
            runFrame(*backend, renderer, {litOcc, occ}, centre)[0];
        std::printf("shadow along the ray: unshadowed=%d  with occluder=%d (must be darker)\n",
                    static_cast<int>(unshadowed.r), static_cast<int>(shadowed.r));
        GRACE_CHECK(shadowed.r < unshadowed.r - 2);
    }

    // A2: both paths agree — the volume pass runs in the shared chain on the
    // HDR scene depth (decision 20).
    {
        PointLight light;
        light.position = vec3{0.0f, 0.0f, 0.5f};
        light.color = vec3{1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;
        light.radius = 0.4f;
        light.volumeDensity = 2.0f;

        Renderer fwd(*backend);
        fwd.setConfig(baseConfig(RenderPath::Forward));
        DrawSubmission sub = groundSub();
        sub.lights.push_back(light);
        const ClearColor f = runFrame(*backend, fwd, {sub}, centre)[0];

        Renderer def(*backend);
        def.setConfig(baseConfig(RenderPath::Deferred));
        const ClearColor d = runFrame(*backend, def, {sub}, centre)[0];
        std::printf("path agreement: forward=%d  deferred=%d  diff=%d\n",
                    static_cast<int>(f.r), static_cast<int>(d.r),
                    std::abs(static_cast<int>(f.r) - static_cast<int>(d.r)));
        GRACE_CHECK(withinTol(f, d, 2));
    }

    // A4: over-cap volumetric lights are rejected, never truncated.
    {
        Renderer renderer(*backend);
        RendererConfig cfg = baseConfig(RenderPath::Deferred);  // deferred cap allows 5 lights
        renderer.setConfig(cfg);
        DrawSubmission sub = groundSub();
        for (int i = 0; i < 5; ++i)
        {
            PointLight light;
            light.position = vec3{0.0f, 0.0f, 0.5f};
            light.volumeDensity = 1.0f;
            sub.lights.push_back(light);
        }
        renderer.beginFrame();
        bool threw = false;
        try
        {
            renderer.submit(sub);
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
        std::printf("over-cap: 5 volumetric lights rejected with invalid_argument: %s\n",
                    threw ? "yes" : "no");
    }

    // A6: default off — a light with volumeDensity 0 changes nothing.
    {
        Renderer renderer(*backend);
        renderer.setConfig(baseConfig(RenderPath::Forward));
        DrawSubmission plain = groundSub();
        DrawSubmission withLight = groundSub();
        PointLight light;
        light.position = vec3{0.0f, 0.0f, 0.5f};
        light.radius = 0.4f;  // ground centre is at d=0.5 > radius: no surface light either
        light.volumeDensity = 0.0f;  // the default
        withLight.lights.push_back(light);
        const ClearColor a = runFrame(*backend, renderer, {plain}, centre)[0];
        const ClearColor b = runFrame(*backend, renderer, {withLight}, centre)[0];
        std::printf("default off: no light=%d  density-0 light=%d (must be equal)\n",
                    static_cast<int>(a.r), static_cast<int>(b.r));
        GRACE_CHECK(withinTol(a, b, 1));
        GRACE_CHECK(withinTol(a, ClearColor{149, 149, 149, 255}, 2));
    }

    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("volumetric");
}
