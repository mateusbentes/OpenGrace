#pragma once

#include <cstdint>

namespace grace::render {

/// Project-owned pixel format enumeration. This is deliberately NOT a
/// graphics-API enum: the backend maps these values onto its own API's
/// constants, and the contract never exposes GL/other-API identifiers.
enum class PixelFormat : std::uint8_t
{
    Rgba8,             ///< 8 bits per channel, RGBA colour attachment
    Depth24Stencil8    ///< 24-bit depth + 8-bit stencil attachment
};

/// Hard cap on colour attachments per render target (milestone 5, decision 9).
/// A G-buffer needs 2-3; 4 is the documented maximum and createRenderTarget
/// THROWS std::invalid_argument when a desc exceeds it — the cap is enforced,
/// never silently applied.
inline constexpr std::uint32_t kMaxColorAttachments = 4;

/// Attachment index selecting the depth attachment in renderTargetTexture().
inline constexpr std::uint32_t kDepthAttachment = 0xFFFFFFFFu;

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
    virtual PixelFormat format() const noexcept = 0;
    virtual bool isDepth() const noexcept = 0;
};

/// Throws std::invalid_argument on an invalid or incomplete desc: zero size,
/// more than kMaxColorAttachments colour attachments (the cap is enforced, not
/// silently applied), or neither a colour nor a depth attachment.
void validateRenderTargetDesc(const RenderTargetDesc& desc);

}  // namespace grace::render
