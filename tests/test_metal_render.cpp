#include "grace/render/metal_render_backend.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "contract_checks.hpp"
// The Metal module's own loader and shader library: the mapping tests
// exercise the pure translation functions and the layout table, both of
// which need no device (the same header paths the backend itself uses).
#include "metal_render_target.hpp"
#include "metal_shader.hpp"
#include "mtl_loader.hpp"
#include "test_support.hpp"
namespace {
using grace::render::MetalBackendConfig;
using grace::render::MetalRenderBackend;
using grace::render::ClearColor;
using grace::render::metal::MTLBlendFactor;
using grace::render::metal::MTLCompareFunction;
using grace::render::metal::MTLIndexType;
using grace::render::metal::MTLLoadAction;
using grace::render::metal::MTLPixelFormat;
using grace::render::metal::MTLPrimitiveType;
using grace::render::metal::MTLSamplerAddressMode;
using grace::render::metal::MTLSamplerMinMagFilter;
using grace::render::metal::MTLStoreAction;
using grace::render::metal::toMtlFormat;
}  // namespace
int main()
{
    // -- Pure mapping checks (no device; run on every platform) ----------
    //
    // The Metal module's translation layer compiles everywhere and the
    // pure mappings are deterministic: these checks run on Linux, the
    // hardware path stays pending until a Mac (milestone 20's recorded
    // validation scope).
    GRACE_CHECK_EQ(toMtlFormat(grace::render::PixelFormat::Rgba8),
                   MTLPixelFormat::Rgba8Unorm);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       toMtlFormat(grace::render::PixelFormat::Rgba8)),
                   70u);
    GRACE_CHECK_EQ(toMtlFormat(grace::render::PixelFormat::Rgba16F),
                   MTLPixelFormat::Rgba16Float);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       toMtlFormat(grace::render::PixelFormat::Rgba16F)),
                   115u);
    GRACE_CHECK_EQ(toMtlFormat(grace::render::PixelFormat::R8),
                   MTLPixelFormat::R8Unorm);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       toMtlFormat(grace::render::PixelFormat::R8)),
                   10u);
    GRACE_CHECK_EQ(
        toMtlFormat(grace::render::PixelFormat::Depth24Stencil8),
        MTLPixelFormat::Depth24Unorm_Stencil8);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(toMtlFormat(
                       grace::render::PixelFormat::Depth24Stencil8)),
                   255u);
    // The published enum values the loader declares (the M19 lesson: the
    // values the device receives must be the API's own).
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       MTLCompareFunction::Less),
                   1u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       MTLCompareFunction::LessEqual),
                   3u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       MTLCompareFunction::Always),
                   7u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(MTLBlendFactor::One), 1u);
    GRACE_CHECK_EQ(
        static_cast<std::uint64_t>(MTLBlendFactor::SourceAlpha), 4u);
    GRACE_CHECK_EQ(
        static_cast<std::uint64_t>(MTLBlendFactor::OneMinusSourceAlpha),
        5u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(MTLLoadAction::Clear), 2u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(MTLLoadAction::Load), 1u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(MTLStoreAction::Store), 1u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       MTLSamplerMinMagFilter::Nearest),
                   0u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       MTLSamplerAddressMode::ClampToEdge),
                   0u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       MTLSamplerAddressMode::Repeat),
                   2u);
    GRACE_CHECK_EQ(
        static_cast<std::uint64_t>(MTLPrimitiveType::Triangle), 3u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(MTLIndexType::UInt32), 1u);
    // The published texture types (the M19 lesson: the values the device
    // receives must be the API's own).
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       grace::render::metal::MTLTextureType::Type2D),
                   2u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       grace::render::metal::MTLTextureType::Type3D),
                   7u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       grace::render::metal::MTLTextureType::Type2DArray),
                   3u);
    // The published storage modes and the packed resource options (the
    // API's own shift: the storage mode lives at bits 4-7, the cache
    // mode at bits 0-3).
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       grace::render::metal::MTLStorageMode::Shared),
                   0u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       grace::render::metal::MTLStorageMode::Private),
                   2u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       grace::render::metal::MTLResourceOptions::Stage),
                   0u);
    GRACE_CHECK_EQ(static_cast<std::uint64_t>(
                       grace::render::metal::MTLResourceOptions::GpuPrivate),
                   32u);
    // The published clear colour struct: doubles, the Metal ABI's one
    // (the Vulkan clear value's floats are not).
    GRACE_CHECK_EQ(sizeof(grace::render::metal::MTLClearColor),
                   4 * sizeof(double));
    // The layout table is shared with the Vulkan library: the same
    // std140 offsets, one table, two shading languages. The Sprite2D
    // program (milestone 23) adds its two stages to the table.
    GRACE_CHECK_EQ(std::size(grace::render::vulkan::kShaderLayouts),
                   29u);
    // -- Device-dependent contract checks (skip on Linux) -----------------
    //
    // initialise() needs the Metal framework and a default device; on a
    // platform without them the loader reports the missing symbol and
    // the device-dependent block skips (CTest code 77, the same shape
    // the Vulkan backend's test uses).
    MetalBackendConfig config;
    config.width = 320;
    config.height = 200;  // non-square on purpose
    config.hidden = true;
    config.readback_center = true;
    config.draw_triangle = true;
    const ClearColor triangle_color{200, 60, 40, 255};
    config.triangle_color = triangle_color;
    std::unique_ptr<MetalRenderBackend> backend;
    try
    {
        backend = std::make_unique<MetalRenderBackend>(config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        // No Metal device in this environment: report as skipped (CTest
        // code 77). The mapping checks above already ran.
        std::printf("SKIP: no Metal device in this environment: %s\n",
                    error.what());
        return 77;
    }
    // (Hardware path: the same clear/readback/contract checks the
    // Vulkan backend's test runs, pending until a Mac executes them.)
    // The 2D texture contract checks run on the hardware path too (the
    // same function the null, OpenGL and Vulkan backends run).
    grace::test::runTexture2DChecks(*backend);
    backend->shutdown();
    return grace::test::summary("test_metal_render");
}
