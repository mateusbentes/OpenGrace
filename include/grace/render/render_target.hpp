#pragma once

#include <cstdint>

namespace grace::render {

/// Project-owned pixel format enumeration. This is deliberately NOT a
/// graphics-API enum: the backend maps these values onto its own API's
/// constants, and the contract never exposes GL/other-API identifiers.
enum class PixelFormat : std::uint8_t
{
    Rgba8,             ///< 8 bits per channel, RGBA colour attachment (linear values, no display encoding)
    Rgba16F,           ///< 16-bit float per channel, RGBA HDR colour attachment (milestone 6a): linear range ~[0, 65504], ~8 bytes/pixel
    R8,                ///< 8-bit single channel (milestone 6c): the SSAO occlusion buffer, ~1 byte/pixel
    Depth24Stencil8    ///< 24-bit depth + 8-bit stencil attachment (sampleable as a texture)
};

/// Hard cap on colour attachments per render target (milestone 5, decision 11).
/// A G-buffer needs 2-3; 4 is the documented maximum and createRenderTarget
/// THROWS std::invalid_argument when a desc exceeds it — the cap is enforced,
/// never silently applied.
inline constexpr std::uint32_t kMaxColorAttachments = 4;

/// Attachment index selecting the depth attachment in renderTargetTexture().
inline constexpr std::uint32_t kDepthAttachment = 0xFFFFFFFFu;

/// Bytes one texel of the given format occupies in memory, as the contract
/// counts them (the same count the backends use for a volume slice): 4 for
/// Rgba8, 8 for Rgba16F, 1 for R8 and 4 for Depth24Stencil8. Declared here so
/// that a caller can size a slice without asking the backend, and so that the
/// number is written once.
constexpr std::uint32_t bytesPerPixel(PixelFormat format) noexcept
{
    switch (format)
    {
    case PixelFormat::Rgba8:
        return 4u;
    case PixelFormat::Rgba16F:
        return 8u;
    case PixelFormat::R8:
        return 1u;
    case PixelFormat::Depth24Stencil8:
        return 4u;
    }
    return 0u;
}

/// Description of a render target. A depth-only target (colorAttachmentCount ==
/// 0, depth attachment present) is what a shadow pass needs: depth is written
/// and sampled, no colour is stored. Multiple colour attachments (MRT) are what
/// a G-buffer needs: one pass writes all of them.
struct RenderTargetDesc
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t colorAttachmentCount = 1;  ///< 0 = depth-only; <= kMaxColorAttachments
    PixelFormat colorFormats[kMaxColorAttachments] = {PixelFormat::Rgba8, PixelFormat::Rgba8,
                                                      PixelFormat::Rgba8, PixelFormat::Rgba8};
    bool depthAttachment = true;
    PixelFormat depthFormat = PixelFormat::Depth24Stencil8;
};

/// Opaque render target handle, owned by the backend that created it. The
/// handle object stays valid until backend shutdown even after release, so
/// that use-after-release can be detected safely as a protocol violation.
class RenderTarget
{
public:
    virtual ~RenderTarget() = default;

    virtual std::uint32_t width() const noexcept = 0;
    virtual std::uint32_t height() const noexcept = 0;
    virtual std::uint32_t colorAttachmentCount() const noexcept = 0;
    virtual bool hasColorAttachment() const noexcept = 0;
    virtual bool hasDepthAttachment() const noexcept = 0;
};

/// Opaque handle to one attachment of a render target, for sampling it as a
/// texture (for example the depth attachment of a shadow map).
class TextureView
{
public:
    virtual ~TextureView() = default;

    virtual std::uint32_t width() const noexcept = 0;
    virtual std::uint32_t height() const noexcept = 0;
    /// Layer count of the view. Every render-target attachment view is one 2D
    /// layer, so the default is 1; a volume created by createTexture3D
    /// overrides it with its own depth (milestone 7, first increment).
    virtual std::uint32_t depth() const noexcept { return 1; }
    virtual PixelFormat format() const noexcept = 0;
    virtual bool isDepth() const noexcept = 0;
};

/// Throws std::invalid_argument on an invalid or incomplete desc: zero size,
/// more than kMaxColorAttachments colour attachments (the cap is enforced, not
/// silently applied), or neither a colour nor a depth attachment.
void validateRenderTargetDesc(const RenderTargetDesc& desc);

/// Description of a 3D texture: a volume the contract can create, fill and
/// sample (milestone 7, first increment). Dimensions are in texels and each of
/// them must be positive.
///
/// Deliberately NOT a depth field on RenderTargetDesc. A volume is a resource,
/// and nothing in the contract renders *into* one yet: a depth field there
/// would be a field no pass could use, and attachment semantics belong to the
/// render target path.
struct Texture3DDesc
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;  ///< texel count along the third axis
    PixelFormat format = PixelFormat::Rgba16F;
};

/// Throws std::invalid_argument when a dimension is zero, or when the format is
/// the 2D depth/stencil format. Depth/stencil is not a colour volume: the
/// driver measured for this project answers glTexImage3D with
/// GL_DEPTH24_STENCIL8 with GL_INVALID_OPERATION, so the contract refuses it up
/// front with a specific message instead of failing later and less clearly.
void validateTexture3DDesc(const Texture3DDesc& desc);

}  // namespace grace::render
