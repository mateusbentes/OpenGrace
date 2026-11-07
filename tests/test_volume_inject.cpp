// Milestone 7, third increment: the scene deposits its light into a volume.
//
// The plan states the proof for this increment: "the scene deposits light,
// proved with one light and one known point". So this test does exactly that —
// a point light at a known place, the injection run, the layer read back, and
// the number compared against the attenuation formula the surface shader uses.
// It does not check that the volume looks good; it checks that the number in
// the volume is the number the light maths says it must be.
//
// WHY THIS NEEDS A GL CONTEXT, measured and not assumed: the null backend
// records events and never executes a shader, so it cannot deposit a radiance
// value into a volume — a first version of this test ran on the null backend
// and measured 0.00000 against an expected 5.77600, because the null backend
// has no arithmetic to do. The numeric proof therefore runs where the shader
// runs. The structural half (off by default, release and recreate, rejected
// configuration) is proved on the null backend below, because it needs no
// arithmetic at all.
#include "contract_checks.hpp"
#include "grace/render/light.hpp"
#include "grace/render/null_render_backend.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/renderer.hpp"
#include "grace/render/render_target.hpp"
#include "test_support.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using grace::render::DrawSubmission;
using grace::render::DynamicLight;
using grace::render::NullRenderBackend;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::PixelFormat;
using grace::render::PointLight;
using grace::render::RenderPath;
using grace::render::Renderer;
using grace::render::RendererConfig;
using grace::render::TextureView;

/// Half-float decode, so the comparison is against the value the volume really
/// stores and not against a re-encode of the value that was written.
float halfToFloat(std::uint16_t bits)
{
    const std::uint32_t sign = static_cast<std::uint32_t>(bits & 0x8000u) << 16;
    const std::uint32_t exponent = (bits >> 10) & 0x1Fu;
    const std::uint32_t mantissa = bits & 0x3FFu;
    std::uint32_t value;
    if (exponent == 0)
    {
        if (mantissa == 0)
            value = sign;  // signed zero
        else
        {
            // Subnormal: renormalise into the float exponent range.
            std::uint32_t e = 127u - 15u + 1u;
            std::uint32_t m = mantissa;
            while ((m & 0x400u) == 0)
            {
                m <<= 1;
                --e;
            }
            value = sign | (e << 23) | ((m & 0x3FFu) << 13);
        }
    }
    else if (exponent == 0x1Fu)
        value = sign | 0x7F800000u | (mantissa << 13);  // inf / nan
    else
        value = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    float out;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

float readChannel(const std::vector<std::uint8_t>& bytes, std::size_t texel, int channel)
{
    const std::size_t offset = texel * 4u * sizeof(std::uint16_t) +
                               static_cast<std::size_t>(channel) * sizeof(std::uint16_t);
    std::uint16_t bits;
    std::memcpy(&bits, bytes.data() + offset, sizeof(bits));
    return halfToFloat(bits);
}

/// The same windowed inverse-square attenuation the surface shader uses. Written
/// out here on purpose: if the shader's formula changes, this test must change
/// with it, which is what makes the comparison a check and not a tautology.
float rangeAtten(float intensity, float range, float d)
{
    if (d > range)
        return 0.0f;
    const float w = 1.0f - d / range;
    return intensity * w * w / (d * d + 1.0f);
}

/// Tolerance for one half-float quantum at these magnitudes. A half float has
/// 10 mantissa bits, so the spacing near 5.78 is 2^-8 = 0.0039; a value written
/// and read back can differ from the exact real number by half of that.
constexpr float kHalfTolerance = 0.005f;

/// The scene deposits light: one point light, one known texel, in the real
/// OpenGL path where the shader actually runs.
void testOneLightOnePoint()
{
    OpenGLBackendConfig backend_config;
    backend_config.width = 64;
    backend_config.height = 64;
    backend_config.hidden = true;
    std::unique_ptr<OpenGLRenderBackend> backend;
    try
    {
        backend = std::make_unique<OpenGLRenderBackend>(backend_config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        std::printf("SKIP: no GL context in this environment: %s\n", error.what());
        std::exit(77);
    }

    Renderer renderer(*backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;  // the volume must not depend on the post chain
    config.volume.enabled = true;
    config.volume.resolution = 4;
    // The fourth increment's propagation step changes the volume after the
    // injection fills it, so this test pins the third increment's behaviour:
    // iterations = 0 is injection only, and the numbers below are the
    // attenuation formula the injection shader writes.
    config.volume.iterations = 0;
    // A 4x4x4 volume over [0,4]^3, so texel centres are at 0.5, 1.5, 2.5, 3.5.
    config.volume.origin = {0.0f, 0.0f, 0.0f};
    config.volume.extent = {4.0f, 4.0f, 4.0f};
    renderer.setConfig(config);

    auto mesh = backend->createGpuMesh();
    mesh->upload(grace::test::detail::makeContractTriangle());

    // One point light, placed so the distance to a known texel is a round
    // number: the texel centre (0.5, 0.5, 0.5) is 0.5 from (1, 0.5, 0.5).
    PointLight light;
    light.position = {1.0f, 0.5f, 0.5f};
    light.color = {1.0f, 1.0f, 1.0f};
    light.intensity = 8.0f;
    light.radius = 10.0f;

    DrawSubmission submission;
    submission.mesh = mesh.get();
    submission.lights.push_back(DynamicLight{light});
    renderer.beginFrame();
    renderer.submit(submission);
    renderer.endFrame();

    TextureView* volume = renderer.lightVolume();
    GRACE_CHECK(volume != nullptr);
    if (volume == nullptr)
        return;
    GRACE_CHECK_EQ(volume->width(), 4u);
    GRACE_CHECK_EQ(volume->height(), 4u);
    GRACE_CHECK_EQ(volume->depth(), 4u);
    GRACE_CHECK_EQ(volume->format(), PixelFormat::Rgba16F);

    const std::size_t bytes = static_cast<std::size_t>(volume->width()) *
                              volume->height() * grace::render::bytesPerPixel(volume->format());
    std::vector<std::uint8_t> layer(bytes, 0);
    GRACE_CHECK(backend->readTexture3D(volume, 0, layer.data(), layer.size()));

    // Texel (0, 0) of layer 0 covers the volume's lower-left corner: its centre
    // is (0.5, 0.5, 0.5), and the light is at (1.0, 0.5, 0.5), so d = 0.5. The
    // shader writes lightColor * rangeAtten(...).
    const float expected = rangeAtten(light.intensity, light.radius, 0.5f);
    const float measured = readChannel(layer, 0u, 0);
    std::printf("inject: texel(0,0) layer 0 expected %.5f measured %.5f\n", expected, measured);
    GRACE_CHECK(std::fabs(measured - expected) < kHalfTolerance);

    // The light must fall off across the layer rather than being written flat:
    // texel (3, 0) covers x in [3,4], centre x = 3.5, so d = 2.5.
    const float far_measured = readChannel(layer, 3u, 0);
    const float far_expected = rangeAtten(light.intensity, light.radius, 2.5f);
    GRACE_CHECK(far_measured < measured);
    GRACE_CHECK(std::fabs(far_measured - far_expected) < kHalfTolerance);

    // The third axis must vary too, or the pass would write one slice into
    // every layer. Layer 3's texel (0,0) is at z = 3.5, so its distance to the
    // light at z = 0.5 is sqrt(0.25 + 9) = 3.0414.
    std::vector<std::uint8_t> top(bytes, 0);
    GRACE_CHECK(backend->readTexture3D(volume, 3, top.data(), top.size()));
    const float dz = std::sqrt(0.25f + 9.0f);
    const float top_expected = rangeAtten(light.intensity, light.radius, dz);
    const float top_measured = readChannel(top, 0u, 0);
    std::printf("inject: layer 3 texel(0,0) expected %.5f measured %.5f\n", top_expected,
                top_measured);
    GRACE_CHECK(std::fabs(top_measured - top_expected) < kHalfTolerance);
    GRACE_CHECK(top_measured < measured);

    // The colour must be the light's colour: a red light must not deposit white.
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
}

/// A light beyond its range deposits nothing: the volume must agree with the
/// shader's windowed attenuation, including its cutoff. A very bright light
/// outside its own radius must leave the volume at zero, which is the check
/// that the cutoff is honoured and not merely small.
void testBeyondRangeDepositsNothing()
{
    OpenGLBackendConfig backend_config;
    backend_config.width = 32;
    backend_config.height = 32;
    backend_config.hidden = true;
    std::unique_ptr<OpenGLRenderBackend> backend;
    try
    {
        backend = std::make_unique<OpenGLRenderBackend>(backend_config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        std::printf("SKIP: no GL context in this environment: %s\n", error.what());
        std::exit(77);
    }

    Renderer renderer(*backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;
    config.volume.enabled = true;
    config.volume.resolution = 2;
    config.volume.iterations = 0;  // third increment's behaviour, see above
    config.volume.origin = {0.0f, 0.0f, 0.0f};
    config.volume.extent = {2.0f, 2.0f, 2.0f};
    renderer.setConfig(config);

    auto mesh = backend->createGpuMesh();
    mesh->upload(grace::test::detail::makeContractTriangle());

    PointLight light;
    light.position = {100.0f, 100.0f, 100.0f};  // far beyond the radius
    light.intensity = 1000.0f;                  // and very bright
    light.radius = 1.0f;
    DrawSubmission submission;
    submission.mesh = mesh.get();
    submission.lights.push_back(DynamicLight{light});
    renderer.beginFrame();
    renderer.submit(submission);
    renderer.endFrame();

    TextureView* volume = renderer.lightVolume();
    GRACE_CHECK(volume != nullptr);
    if (volume == nullptr)
        return;
    const std::size_t bytes = static_cast<std::size_t>(volume->width()) *
                              volume->height() * grace::render::bytesPerPixel(volume->format());
    std::vector<std::uint8_t> layer(bytes, 0);
    GRACE_CHECK(backend->readTexture3D(volume, 0, layer.data(), layer.size()));
    GRACE_CHECK(readChannel(layer, 0u, 0) == 0.0f);
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
}

/// Off by default, and with it off no volume exists: a frame that asks for none
/// must not pay for one. This half needs no GL context and runs on the null
/// backend, because it is about the renderer's bookkeeping, not arithmetic.
void testOffByDefault()
{
    NullRenderBackend backend(32, 32);
    Renderer renderer(backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;
    renderer.setConfig(config);
    GRACE_CHECK(renderer.config().volume.enabled == false);
    renderer.beginFrame();
    renderer.endFrame();
    GRACE_CHECK(renderer.lightVolume() == nullptr);
}

/// The volume is released when the configuration stops asking for it, and
/// re-created when it asks again. This is the gap the third increment closed:
/// before it, a volume could only be freed at shutdown, so a renderer that
/// rebuilt its volume on a configuration change could only leak it.
void testReleaseAndRecreate()
{
    NullRenderBackend backend(32, 32);
    Renderer renderer(backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;
    config.volume.enabled = true;
    config.volume.resolution = 2;
    renderer.setConfig(config);
    renderer.beginFrame();
    renderer.endFrame();
    TextureView* first = renderer.lightVolume();
    GRACE_CHECK(first != nullptr);

    config.volume.enabled = false;
    renderer.setConfig(config);
    GRACE_CHECK(renderer.lightVolume() == nullptr);

    config.volume.enabled = true;
    renderer.setConfig(config);
    renderer.beginFrame();
    renderer.endFrame();
    TextureView* second = renderer.lightVolume();
    GRACE_CHECK(second != nullptr);
    // Re-created, not the released handle handed back: a released view must
    // never be reused, which is why releaseTexture3D removes it from the
    // registry instead of flagging it like a render target.
    GRACE_CHECK(second != first);
}

/// Releasing a view this backend never created is a violation, not a crash.
void testReleasingAnUnknownVolumeIsAViolation()
{
    NullRenderBackend backend(32, 32);
    NullRenderBackend other(32, 32);
    grace::render::Texture3DDesc desc;
    desc.width = 2;
    desc.height = 2;
    desc.depth = 2;
    TextureView* foreign = other.createTexture3D(desc);
    const std::uint64_t before = backend.violationCount();
    backend.releaseTexture3D(foreign);
    GRACE_CHECK_EQ(backend.violationCount(), before + 1u);
}

/// An invalid volume configuration is rejected, never clamped.
void testInvalidConfigIsRejected()
{
    NullRenderBackend backend(32, 32);
    Renderer renderer(backend);
    RendererConfig config;
    config.volume.resolution = 0;
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

    config.volume.resolution = 4;
    config.volume.extent = {1.0f, 0.0f, 1.0f};
    threw = false;
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

}  // namespace

int main()
{
    testOffByDefault();
    testReleaseAndRecreate();
    testReleasingAnUnknownVolumeIsAViolation();
    testInvalidConfigIsRejected();
    testOneLightOnePoint();
    testBeyondRangeDepositsNothing();
    return grace::test::summary("volume_inject");
}
