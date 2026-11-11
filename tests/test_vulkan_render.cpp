#include "grace/render/null_render_backend.hpp"
#include "grace/render/vulkan_render_backend.hpp"
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
using grace::render::ReadbackSample;
using grace::render::VulkanBackendConfig;
using grace::render::VulkanRenderBackend;

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
    VulkanBackendConfig config;
    config.width = 320;
    config.height = 200;  // non-square on purpose
    config.hidden = true;
    config.readback_center = true;
    config.draw_triangle = true;
    const ClearColor triangle_color{200, 60, 40, 255};
    config.triangle_color = triangle_color;
    std::unique_ptr<VulkanRenderBackend> backend;
    try
    {
        backend = std::make_unique<VulkanRenderBackend>(config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        // No Vulkan device in this environment: report as skipped (CTest
        // code 77).
        std::printf("SKIP: no Vulkan device in this environment: %s\n",
                    error.what());
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
    backend->readback();  // 6a: clear() no longer captures; capture is explicit
    GRACE_CHECK(backend->hasCenterPixel());
    GRACE_CHECK(withinOne(backend->centerPixel(), triangle_color));
    const std::vector<ReadbackSample> samples = backend->lastReadback();
    GRACE_CHECK_EQ(samples.size(), 1u);
    if (!samples.empty())
        GRACE_CHECK(withinOne(samples[0].color, clear_color));

    // Depth and stencil clear values reach the framebuffer.
    backend->setClearBuffers(static_cast<std::uint32_t>(
        ClearBuffer::Color | ClearBuffer::Depth | ClearBuffer::Stencil));
    backend->setClearDepth(0.25f);
    backend->setClearStencil(7);
    backend->queueReadback(288, 180);
    backend->clear();
    backend->readback();
    GRACE_CHECK(std::fabs(backend->centerDepth() - 0.25f) < 0.001f);
    GRACE_CHECK_EQ(backend->centerStencil(), 7);
    const std::vector<ReadbackSample> depth_samples = backend->lastReadback();
    GRACE_CHECK(depth_samples.size() == 1u);
    GRACE_CHECK(std::fabs(depth_samples[0].depth - 0.25f) < 0.001f);
    GRACE_CHECK_EQ(depth_samples[0].stencil, 7);

    // A clear honouring only the colour buffer must leave depth untouched.
    backend->setClearBuffers(
        static_cast<std::uint32_t>(ClearBuffer::Color));
    backend->setClearDepth(0.75f);
    const ClearColor clear_color2{200, 200, 200, 255};
    backend->setClearColor(clear_color2);
    backend->queueReadback(288, 180);
    backend->clear();
    backend->readback();
    GRACE_CHECK(std::fabs(backend->centerDepth() - 0.25f) < 0.001f);
    const std::vector<ReadbackSample> color_only_samples =
        backend->lastReadback();
    GRACE_CHECK(color_only_samples.size() == 1u);
    GRACE_CHECK(withinOne(color_only_samples[0].color, clear_color2));
    GRACE_CHECK(withinOne(backend->centerPixel(), triangle_color));

    GRACE_CHECK_EQ(backend->violationCount(), 0u);
    // Milestone 19: the depth-only shadow target's view binds on the
    // contract's cascade unit (the layout table's unit rule), and a
    // pass that clears into it records without violations — the same
    // bind-and-render path the shadow passes run on hardware.
    {
        grace::render::RenderTargetDesc shadow_desc;
        shadow_desc.width = 64;
        shadow_desc.height = 64;
        shadow_desc.colorAttachmentCount = 0;
        shadow_desc.depthAttachment = true;
        shadow_desc.depthFormat =
            grace::render::PixelFormat::Depth24Stencil8;
        grace::render::RenderTarget* shadow_target =
            backend->createRenderTarget(shadow_desc);
        GRACE_CHECK(shadow_target != nullptr);
        grace::render::TextureView* shadow_view =
            backend->renderTargetTexture(shadow_target);
        GRACE_CHECK(shadow_view != nullptr);
        GRACE_CHECK(shadow_view->isDepth());
        backend->bindTexture(shadow_view, 0u);
        GRACE_CHECK_EQ(backend->violationCount(), 0u);
        backend->beginRenderPass(shadow_target);
        backend->clear();
        backend->endRenderPass();
        GRACE_CHECK_EQ(backend->violationCount(), 0u);
        backend->releaseRenderTarget(shadow_target);
    }
    // The describe() string names the API and the device (the contract's
    // renderer identity, the same fields the OpenGL backend's describe()
    // carries).
    GRACE_CHECK(backend->describe().find("vulkan") == 0);
    GRACE_CHECK(backend->describe().find("renderer=") != std::string::npos);
    backend->shutdown();

    // The same shared contract checks produce the same lifecycle on the null
    // backend and on the Vulkan backend.
    const ClearColor contract_color{1, 2, 3, 4};
    NullRenderBackend null_backend(64, 48);
    grace::test::runContractChecks(null_backend, contract_color, 3);

    try
    {
        VulkanBackendConfig contract_config;
        contract_config.width = 160;
        contract_config.height = 120;
        contract_config.readback_center = false;
        contract_config.draw_triangle = false;
        VulkanRenderBackend vulkan_backend(contract_config);
        vulkan_backend.initialise();
        grace::test::runContractChecks(vulkan_backend, contract_color, 3);
        vulkan_backend.shutdown();
    }
    catch (const std::exception& error)
    {
        std::printf("SKIP: no Vulkan device for the contract checks: %s\n",
                    error.what());
        return 77;
    }
    // The volume checks run on a fresh backend: the contract checks end
    // with shutdown() (their post-shutdown misuse block) and expect a
    // backend with no prior violations.
    try
    {
        VulkanBackendConfig volume_config;
        volume_config.width = 160;
        volume_config.height = 120;
        VulkanRenderBackend volume_backend(volume_config);
        volume_backend.initialise();
        grace::test::runTexture3DChecks(volume_backend);
        grace::test::runVolumeLayerChecks(volume_backend);
        volume_backend.shutdown();
    }
    catch (const std::exception& error)
    {
        std::printf("SKIP: no Vulkan device for the volume checks: %s\n",
                    error.what());
        return 77;
    }

    return grace::test::summary("test_vulkan_render");
}
