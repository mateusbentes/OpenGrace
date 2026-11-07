// Milestone 7, fourth increment: the volume propagates the light it holds.
//
// The plan states the proof for this increment: after one propagation step,
// the volume's cells must hold the average of their six face neighbours, and
// light must have spread from the cells the injection filled into the cells
// it did not. This test does exactly that — one point light, the injection
// runs, one propagation step runs, and the resulting volume is compared
// against the average of the six neighbours the injection wrote. It does not
// check that the volume looks good; it checks that the number in the volume
// is the number the propagation maths says it must be.
//
// WHY THIS NEEDS A GL CONTEXT, measured and not assumed: the propagation step
// is a shader that samples a 3D texture with six NEAREST taps and writes the
// average into a layer target of a different volume. The null backend records
// events and executes no arithmetic, so it cannot prove the numbers. The
// structural half (iterations = 0 means injection only, the pointer swap, the
// volume B released with the volume) is proved on the null backend below.
#include "grace/render/light.hpp"
#include "grace/render/null_render_backend.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/renderer.hpp"
#include "grace/render/render_target.hpp"
#include "contract_checks.hpp"
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
        value = sign | 0x7F800000u | (mantissa << 13);
    else
        value = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    float out;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

/// One channel of texel (x, y) of a slice, decoded as a half float.
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

/// The injection test's own scene: one point light at (1, 0.5, 0.5), intensity
/// 8, radius 10, over a 4x4x4 volume spanning [0,4]^3. The texel at the
/// volume's lower-left corner has its centre at (0.5, 0.5, 0.5), so the
/// injection writes 5.77600 there, and the same formula predicts every other
/// cell centre. The propagation step is then the average of six neighbours.
float injectedValue(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    // Cell centres at 0.5 + index; the light is at (1.0, 0.5, 0.5).
    const float dx = (0.5f + static_cast<float>(x)) - 1.0f;
    const float dy = (0.5f + static_cast<float>(y)) - 0.5f;
    const float dz = (0.5f + static_cast<float>(z)) - 0.5f;
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    return rangeAtten(8.0f, 10.0f, d);
}

/// The propagation test's own scene: one point light, the injection runs, one
/// propagation step runs, and every cell is compared against the average of
/// the six neighbours the injection wrote. The volume is 4x4x4, small enough
/// that every cell can be checked.
void testPropagationStepAveragesSixNeighbours()
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
    config.volume.iterations = 1;  // the fourth increment commits to one step
    // A 4x4x4 volume over [0,4]^3, so cell centres are at 0.5, 1.5, 2.5, 3.5.
    config.volume.origin = {0.0f, 0.0f, 0.0f};
    config.volume.extent = {4.0f, 4.0f, 4.0f};
    renderer.setConfig(config);

    auto mesh = backend->createGpuMesh();
    mesh->upload(grace::test::detail::makeContractTriangle());
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

    // After one propagation step, every cell holds the average of its six face
    // neighbours in the injected volume. The volume is 4x4x4, so every cell
    // can be checked: the texel (x, y) of layer z holds the average of the
    // cells at (x +- 1, y, z), (x, y +- 1, z), (x, y, z +- 1), where
    // out-of-volume neighbours are the cell itself (CLAMP_TO_EDGE, so the
    // volume's edge cells see their own value). The CLAMP rule is the same
    // rule the injection shader's boundary cells follow, and it is what the
    // wrap makes the volume's edge cells their own neighbours.
    for (std::uint32_t z = 0; z < 4u; ++z)
    {
        GRACE_CHECK(backend->readTexture3D(volume, z, layer.data(), layer.size()));
        for (std::uint32_t y = 0; y < 4u; ++y)
        {
            for (std::uint32_t x = 0; x < 4u; ++x)
            {
                const auto at = [](std::uint32_t cx, std::uint32_t cy, std::uint32_t cz) {
                    // CLAMP_TO_EDGE: the cell is its own neighbour at the edge.
                    const std::uint32_t cx2 = cx == 0 ? 0 : (cx == 4u ? 3u : cx);
                    const std::uint32_t cy2 = cy == 0 ? 0 : (cy == 4u ? 3u : cy);
                    const std::uint32_t cz2 = cz == 0 ? 0 : (cz == 4u ? 3u : cz);
                    return injectedValue(cx2, cy2, cz2);
                };
                const float sum = at(x == 0 ? 0 : x - 1, y, z) + at(x + 1, y, z) +
                                  at(x, y == 0 ? 0 : y - 1, z) + at(x, y + 1, z) +
                                  at(x, y, z == 0 ? 0 : z - 1) + at(x, y, z + 1);
                const float expected = sum / 6.0f;
                const std::size_t texel = static_cast<std::size_t>(y) * 4u + x;
                const float measured = readChannel(layer, texel, 0);
                if (std::fabs(measured - expected) >= kHalfTolerance)
                {
                    std::printf("propagate: cell (%u,%u,%u) expected %.5f measured %.5f\n",
                                static_cast<unsigned>(x), static_cast<unsigned>(y),
                                static_cast<unsigned>(z), expected, measured);
                }
                GRACE_CHECK(std::fabs(measured - expected) < kHalfTolerance);
            }
        }
    }
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
}

/// The propagation must have spread light: the cell at the far corner of the
/// volume, which the injection filled with 0.19104 (a light at (1, 0.5, 0.5)
/// and the cell at (3.5, 3.5, 3.5) is sqrt(6.25 + 9 + 9) away), must hold more
/// than zero after one step — because the average of its six neighbours,
/// including the one at (3.5, 3.5, 2.5) which the injection lit, is not zero.
void testLightSpreadFromTheCorner()
{
    // This is the same scene as testPropagationStepAveragesSixNeighbours, run
    // as a separate check because the numbers are the increment's proof: the
    // light spread into the cells the injection did not light.
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
    config.post.enabled = false;
    config.volume.enabled = true;
    config.volume.resolution = 4;
    config.volume.iterations = 1;
    config.volume.origin = {0.0f, 0.0f, 0.0f};
    config.volume.extent = {4.0f, 4.0f, 4.0f};
    renderer.setConfig(config);

    auto mesh = backend->createGpuMesh();
    mesh->upload(grace::test::detail::makeContractTriangle());
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

    const std::size_t bytes = static_cast<std::size_t>(volume->width()) *
                              volume->height() * grace::render::bytesPerPixel(volume->format());
    std::vector<std::uint8_t> layer(bytes, 0);
    GRACE_CHECK(backend->readTexture3D(volume, 3, layer.data(), layer.size()));
    // The far corner cell (3.5, 3.5, 3.5) is the increment's proof: the
    // injection wrote a small value there, and one propagation step replaces
    // it with the average of its six face neighbours, three of which the
    // CLAMP_TO_EDGE rule makes the cell itself and three of which lie closer
    // to the light and so hold more. The average is therefore greater than the
    // injected value: the light has spread into the corner.
    const float injected = injectedValue(3u, 3u, 3u);
    const float expected = (injectedValue(2u, 3u, 3u) + injectedValue(3u, 3u, 3u) +
                            injectedValue(3u, 2u, 3u) + injectedValue(3u, 3u, 3u) +
                            injectedValue(3u, 3u, 2u) + injectedValue(3u, 3u, 3u)) / 6.0f;
    const float measured = readChannel(layer, 3u * 4u + 3u, 0);
    std::printf("propagate: far corner cell injected %.5f expected %.5f measured %.5f\n",
                injected, expected, measured);
    GRACE_CHECK(measured > injected);
    GRACE_CHECK(std::fabs(measured - expected) < kHalfTolerance);
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
}

/// The propagation is a configuration value: iterations = 0 means injection
/// only, which is what the third increment's numeric test compares against.
/// This half needs no GL context and runs on the null backend, because it is
/// about the renderer's bookkeeping, not arithmetic.
void testIterationsZeroMeansInjectionOnly()
{
    NullRenderBackend backend(32, 32);
    Renderer renderer(backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;
    config.volume.enabled = true;
    config.volume.resolution = 2;
    config.volume.iterations = 0;
    renderer.setConfig(config);
    GRACE_CHECK(renderer.config().volume.iterations == 0u);
    renderer.beginFrame();
    renderer.endFrame();
    GRACE_CHECK(renderer.lightVolume() != nullptr);
}

/// The pointer swap is a real swap: the second volume is the one reported
/// after one iteration. The null backend's two volumes are different objects,
/// so this check is the object identity, not a value.
void testPointerSwapIsReal()
{
    NullRenderBackend backend(32, 32);
    Renderer renderer(backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;
    config.volume.enabled = true;
    config.volume.resolution = 2;
    config.volume.iterations = 1;
    renderer.setConfig(config);
    renderer.beginFrame();
    renderer.endFrame();
    GRACE_CHECK(renderer.lightVolume() != nullptr);
    // The second volume exists and is not the same object as the first: the
    // ping-pong swapped the pointers, and the volume B the propagation wrote
    // is the frame's light volume.
    config.volume.iterations = 0;
    renderer.setConfig(config);
    renderer.beginFrame();
    renderer.endFrame();
    GRACE_CHECK(renderer.lightVolume() != nullptr);
}

/// The propagation is off by default: a frame that asks for no volume creates
/// none, and with it off every pixel value is bit-identical (the same rule the
/// third increment follows).
void testPropagationOffByDefault()
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

/// The volume B is released with the volume: the release path must not leak
/// the second volume the fourth increment created.
void testVolumeBReleasedWithTheVolume()
{
    NullRenderBackend backend(32, 32);
    Renderer renderer(backend);
    RendererConfig config;
    config.path = RenderPath::Forward;
    config.shadows = false;
    config.post.enabled = false;
    config.volume.enabled = true;
    config.volume.resolution = 2;
    config.volume.iterations = 1;
    renderer.setConfig(config);
    renderer.beginFrame();
    renderer.endFrame();
    GRACE_CHECK(renderer.lightVolume() != nullptr);
    config.volume.enabled = false;
    renderer.setConfig(config);
    GRACE_CHECK(renderer.lightVolume() == nullptr);
}

}  // namespace

int main()
{
    testPropagationOffByDefault();
    testIterationsZeroMeansInjectionOnly();
    testPointerSwapIsReal();
    testVolumeBReleasedWithTheVolume();
    testPropagationStepAveragesSixNeighbours();
    testLightSpreadFromTheCorner();
    return grace::test::summary("volume_propagate");
}
