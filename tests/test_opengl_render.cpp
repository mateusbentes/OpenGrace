#include "grace/render/null_render_backend.hpp"
#include "grace/render/opengl_render_backend.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "contract_checks.hpp"
#include "test_support.hpp"

namespace {

using grace::render::ClearBuffer;
using grace::render::ClearColor;
using grace::render::NullRenderBackend;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::ReadbackSample;

bool withinOne(ClearColor actual, ClearColor expected)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= 1 && diff(actual.g, expected.g) <= 1 &&
           diff(actual.b, expected.b) <= 1 && diff(actual.a, expected.a) <= 1;
}

}  // namespace

int main()
{
    OpenGLBackendConfig config;
    config.width = 320;
    config.height = 200;  // non-square on purpose
    config.hidden = true;
    config.readback_center = true;
    config.draw_triangle = true;
    const ClearColor triangle_color{200, 60, 40, 255};
    config.triangle_color = triangle_color;

    std::unique_ptr<OpenGLRenderBackend> backend;
    try
    {
        backend = std::make_unique<OpenGLRenderBackend>(config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        // No GL context in this environment: report as skipped (CTest code 77).
        std::printf("SKIP: no GL context in this environment: %s\n", error.what());
        return 77;
    }

    // The clear colour reaches the framebuffer within one unit per channel:
    // the centre is covered by the triangle, an off-centre pixel is not.
    // NDC (0.8, 0.8) maps to pixel (288, 180) from the bottom-left in a
    // 320x200 window, outside the triangle.
    const ClearColor clear_color{10, 20, 30, 255};
    backend->setClearColor(clear_color);
    backend->queueReadback(288, 180);
    backend->clear();

    GRACE_CHECK(backend->hasCenterPixel());
    GRACE_CHECK(withinOne(backend->centerPixel(), triangle_color));
    const std::vector<ReadbackSample> samples = backend->lastReadback();
    GRACE_CHECK(samples.size() == 1u);
    GRACE_CHECK(withinOne(samples[0].color, clear_color));

    // Depth and stencil clear values reach the framebuffer.
    backend->setClearBuffers(
        static_cast<std::uint32_t>(ClearBuffer::Color | ClearBuffer::Depth | ClearBuffer::Stencil));
    backend->setClearDepth(0.25f);
    backend->setClearStencil(7);
    backend->queueReadback(288, 180);
    backend->clear();
    GRACE_CHECK(std::fabs(backend->centerDepth() - 0.25f) < 0.001f);
    GRACE_CHECK_EQ(backend->centerStencil(), 7);
    const std::vector<ReadbackSample> depth_samples = backend->lastReadback();
    GRACE_CHECK(depth_samples.size() == 1u);
    GRACE_CHECK(std::fabs(depth_samples[0].depth - 0.25f) < 0.001f);
    GRACE_CHECK_EQ(depth_samples[0].stencil, 7);

    // A clear honouring only the colour buffer must leave depth untouched.
    backend->setClearBuffers(static_cast<std::uint32_t>(ClearBuffer::Color));
    backend->setClearDepth(0.75f);
    const ClearColor clear_color2{200, 200, 200, 255};
    backend->setClearColor(clear_color2);
    backend->queueReadback(288, 180);
    backend->clear();
    GRACE_CHECK(std::fabs(backend->centerDepth() - 0.25f) < 0.001f);
    const std::vector<ReadbackSample> color_only_samples = backend->lastReadback();
    GRACE_CHECK(color_only_samples.size() == 1u);
    GRACE_CHECK(withinOne(color_only_samples[0].color, clear_color2));
    GRACE_CHECK(withinOne(backend->centerPixel(), triangle_color));

    GRACE_CHECK_EQ(backend->violationCount(), 0u);
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    // The same shared contract checks produce the same lifecycle on the null
    // backend and on the OpenGL backend.
    const ClearColor contract_color{1, 2, 3, 4};
    NullRenderBackend null_backend(64, 48);
    grace::test::runContractChecks(null_backend, contract_color, 3);
    const std::vector<std::string> expected_events{
        "setClearColor(1,2,3,4)",
        "clear(1,2,3,4)",
        "clear(1,2,3,4)",
        "clear(1,2,3,4)",
        "queueReadback(1,1)",
        "clear(1,2,3,4)",
        "readback",
        "createShaderProgram",
        "createGpuMesh",
        "upload",
        "draw",
        "use",
        "drawIndexed",
        "shutdown",
        "draw",    // post-shutdown: violation
        "readback",  // post-shutdown: violation
        "clear(1,2,3,4)"};
    GRACE_CHECK(null_backend.events() == expected_events);

    OpenGLBackendConfig parity_config;
    parity_config.width = 64;
    parity_config.height = 48;
    parity_config.hidden = true;
    std::unique_ptr<OpenGLRenderBackend> gl_backend;
    try
    {
        gl_backend = std::make_unique<OpenGLRenderBackend>(parity_config);
        gl_backend->initialise();
    }
    catch (const std::exception& error)
    {
        std::printf("SKIP: no GL context in this environment: %s\n", error.what());
        return 77;
    }
    grace::test::runContractChecks(*gl_backend, contract_color, 3);
    GRACE_CHECK_EQ(gl_backend->violationCount(), 3u);  // draw + readback + clear after shutdown
    GRACE_CHECK_EQ(gl_backend->glErrorCount(), 0u);

    return grace::test::summary("opengl_render");
}
