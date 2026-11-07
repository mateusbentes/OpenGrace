// 3D texture contract test (milestone 7, first increment).
//
// This suite links the null backend only, so it runs with no GL context and no
// window: the volume capability is proved where it can be proved everywhere.
// The OpenGL half of the same checks runs inside test_opengl_render, through
// the same runTexture3DChecks function, which is what makes the two results
// comparable.
#include "grace/render/null_render_backend.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "contract_checks.hpp"
#include "test_support.hpp"

namespace {

using grace::render::bytesPerPixel;
using grace::render::NullRenderBackend;
using grace::render::PixelFormat;
using grace::render::Texture3DDesc;
using grace::render::TextureView;

/// Bytes in one slice of a volume, as the contract counts them.
std::size_t sliceBytes(const Texture3DDesc& desc)
{
    return static_cast<std::size_t>(desc.width) * desc.height * bytesPerPixel(desc.format);
}

void testRoundTripPerFormat()
{
    NullRenderBackend backend(32, 32);
    const PixelFormat formats[] = {PixelFormat::Rgba8, PixelFormat::Rgba16F, PixelFormat::R8};
    for (const PixelFormat format : formats)
    {
        Texture3DDesc desc;
        desc.width = 4;
        desc.height = 4;
        desc.depth = 3;
        desc.format = format;
        TextureView* view = backend.createTexture3D(desc);
        GRACE_CHECK(view != nullptr);
        GRACE_CHECK(view->format() == format);

        const std::size_t bytes = sliceBytes(desc);
        bool all_match = true;
        for (std::uint32_t layer = 0; layer < desc.depth; ++layer)
        {
            std::vector<std::uint8_t> written(bytes);
            for (std::size_t i = 0; i < bytes; ++i)
                written[i] = static_cast<std::uint8_t>((layer * 97u + i * 31u + 5u) & 0xFFu);
            backend.fillTexture3D(view, layer, written.data(), written.size());
            std::vector<std::uint8_t> read(bytes, 0);
            if (!backend.readTexture3D(view, layer, read.data(), read.size()))
                all_match = false;
            all_match = all_match && (read == written);
        }
        GRACE_CHECK(all_match);
    }
}

/// A width whose row length is not a multiple of four: the case where a backend
/// that ignores row alignment would return the wrong bytes. Measured on the
/// OpenGL driver used by this project: an R8 3x3 slice round-trips exactly with
/// alignment 1 and does not with the default alignment of 4.
void testUnalignedRowWidth()
{
    NullRenderBackend backend(32, 32);
    Texture3DDesc desc;
    desc.width = 3;
    desc.height = 3;
    desc.depth = 2;
    desc.format = PixelFormat::R8;
    TextureView* view = backend.createTexture3D(desc);
    GRACE_CHECK(view != nullptr);
    const std::size_t bytes = sliceBytes(desc);
    GRACE_CHECK_EQ(bytes, 9u);

    std::vector<std::uint8_t> written(bytes);
    for (std::size_t i = 0; i < bytes; ++i)
        written[i] = static_cast<std::uint8_t>(i + 1u);
    backend.fillTexture3D(view, 0, written.data(), written.size());
    std::vector<std::uint8_t> read(bytes, 0);
    GRACE_CHECK(backend.readTexture3D(view, 0, read.data(), read.size()));
    GRACE_CHECK(read == written);
}

void testRejections()
{
    NullRenderBackend backend(32, 32);
    Texture3DDesc desc;
    desc.width = 4;
    desc.height = 4;
    desc.depth = 2;
    desc.format = PixelFormat::Rgba8;
    TextureView* view = backend.createTexture3D(desc);
    const std::size_t bytes = sliceBytes(desc);

    // Out-of-range layer and a mismatched byte count: the write throws, the
    // read reports false, and neither counts as a protocol violation because
    // both are documented argument errors rather than misuse of the device.
    bool threw = false;
    std::vector<std::uint8_t> scratch(bytes, 0);
    try
    {
        backend.fillTexture3D(view, desc.depth, scratch.data(), scratch.size());
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
    threw = false;
    try
    {
        backend.fillTexture3D(view, 0, scratch.data(), scratch.size() - 1u);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
    GRACE_CHECK(!backend.readTexture3D(view, desc.depth, scratch.data(), scratch.size()));
    GRACE_CHECK(!backend.readTexture3D(view, 0, scratch.data(), scratch.size() - 1u));

    // A zero dimension is rejected at creation, for each dimension in turn.
    for (int axis = 0; axis < 3; ++axis)
    {
        Texture3DDesc bad = desc;
        if (axis == 0)
            bad.width = 0;
        else if (axis == 1)
            bad.height = 0;
        else
            bad.depth = 0;
        threw = false;
        try
        {
            static_cast<void>(backend.createTexture3D(bad));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
    }

    // The 2D depth/stencil format cannot describe a volume.
    Texture3DDesc depthFormat = desc;
    depthFormat.format = PixelFormat::Depth24Stencil8;
    threw = false;
    try
    {
        static_cast<void>(backend.createTexture3D(depthFormat));
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
}

/// A volume outlives a render pass and is not affected by one: it owns no
/// framebuffer, so nothing a pass does may disturb its contents. This is the
/// property the null backend can check cheaply and the OpenGL backend is
/// checked for in test_opengl_render.
void testVolumeSurvivesARenderPass()
{
    NullRenderBackend backend(32, 32);
    Texture3DDesc desc;
    desc.width = 4;
    desc.height = 4;
    desc.depth = 2;
    desc.format = PixelFormat::Rgba8;
    TextureView* view = backend.createTexture3D(desc);
    const std::size_t bytes = sliceBytes(desc);
    std::vector<std::uint8_t> written(bytes);
    for (std::size_t i = 0; i < bytes; ++i)
        written[i] = static_cast<std::uint8_t>((i * 13u + 29u) & 0xFFu);
    backend.fillTexture3D(view, 0, written.data(), written.size());

    grace::render::RenderTargetDesc target;
    target.width = 16;
    target.height = 16;
    grace::render::RenderTarget* renderTarget = backend.createRenderTarget(target);
    backend.beginRenderPass(renderTarget);
    backend.clear();
    backend.endRenderPass();
    backend.releaseRenderTarget(renderTarget);

    std::vector<std::uint8_t> read(bytes, 0);
    GRACE_CHECK(backend.readTexture3D(view, 0, read.data(), read.size()));
    GRACE_CHECK(read == written);
}

}  // namespace

int main()
{
    testRoundTripPerFormat();
    testUnalignedRowWidth();
    testRejections();
    testVolumeSurvivesARenderPass();
    return grace::test::summary("texture3d");
}
