#pragma once
#include <cstdint>
#include <vector>
#include "grace/render/render_target.hpp"
#include "mtl_loader.hpp"
namespace grace {
namespace render {
namespace metal {
/// Texture view of one attachment of a MtlRenderTarget (or of a volume).
/// The backend creates and releases the underlying MTLTexture; the view
/// handle itself is plain data the engine holds through the contract.
class MtlTextureView final : public TextureView
{
public:
    MtlTextureView() = default;
    MtlTextureView(MTLTextureHandle texture, std::uint32_t width,
                   std::uint32_t height, PixelFormat format, bool isDepth,
                   std::uint32_t depth = 1)
        : m_texture(texture), m_width(width), m_height(height),
          m_format(format), m_is_depth(isDepth), m_depth(depth)
    {
    }
    std::uint32_t width() const noexcept override { return m_width; }
    std::uint32_t height() const noexcept override { return m_height; }
    std::uint32_t depth() const noexcept override { return m_depth; }
    PixelFormat format() const noexcept override { return m_format; }
    bool isDepth() const noexcept override { return m_is_depth; }
    MTLTextureHandle texture() const noexcept { return m_texture; }
private:
    MTLTextureHandle m_texture = nullptr;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    PixelFormat m_format = PixelFormat::Rgba8;
    bool m_is_depth = false;
    std::uint32_t m_depth = 1;
};
/// Metal render target: up to kMaxColorAttachments colour attachments plus
/// an optional depth/stencil attachment. The backend creates the
/// MTLTextures; this handle holds them and the contract-visible views. The
/// project PixelFormat values are mapped onto MTLPixelFormat inside the
/// Metal module only.
class MtlRenderTarget final : public RenderTarget
{
public:
    MtlRenderTarget(const RenderTargetDesc& desc)
        : m_desc(desc), m_color_views(desc.colorAttachmentCount)
    {
    }
    /// Layer-target mode (milestone 7, second increment): the single
    /// colour attachment is one layer of an existing volume. The target
    /// borrows the volume's texture and never releases it, so releasing
    /// the target leaves the volume intact — the same ownership rule the
    /// OpenGL and the Vulkan backends state.
    MtlRenderTarget(const VolumeLayerDesc& desc)
        : m_volume_desc(desc),
          m_color_views(1), m_borrows_volume(true)
    {
        m_desc.width = desc.volume.width;
        m_desc.height = desc.volume.height;
        m_desc.colorAttachmentCount = 1;
        m_desc.colorFormats[0] = desc.volume.format;
        m_desc.depthAttachment = false;
    }
    std::uint32_t width() const noexcept override { return m_desc.width; }
    std::uint32_t height() const noexcept override { return m_desc.height; }
    std::uint32_t colorAttachmentCount() const noexcept override
    {
        return m_desc.colorAttachmentCount;
    }
    bool hasColorAttachment() const noexcept override
    {
        return m_desc.colorAttachmentCount > 0;
    }
    bool hasDepthAttachment() const noexcept override
    {
        return m_desc.depthAttachment;
    }
    TextureView* colorView(std::uint32_t index)
    {
        return index < m_color_views.size() ? &m_color_views[index]
                                            : nullptr;
    }
    TextureView* depthView()
    {
        return m_desc.depthAttachment ? &m_depth_view : nullptr;
    }
    /// The backend fills the contract-facing views when it creates the
    /// device objects (each view reports its attachment's format and kind).
    void setColorView(std::uint32_t index, MTLTextureHandle texture,
                      std::uint32_t width, std::uint32_t height,
                      PixelFormat format, bool isDepth)
    {
        if (index < m_color_views.size())
        {
            m_color_views[index] =
                MtlTextureView(texture, width, height, format, isDepth);
        }
    }
    void setDepthView(MTLTextureHandle texture, std::uint32_t width,
                      std::uint32_t height, PixelFormat format, bool isDepth)
    {
        m_depth_view =
            MtlTextureView(texture, width, height, format, isDepth);
    }
    /// The attachment format of the target's colour attachment i (the
    /// project's PixelFormat, mapped onto MTLPixelFormat by the backend).
    PixelFormat colorFormat(std::uint32_t index) const
    {
        return m_desc.colorFormats[index];
    }
    /// The target's descriptor (the depth view reads its depthFormat).
    const RenderTargetDesc& desc() const noexcept { return m_desc; }
    /// The borrowed volume's descriptor in layer-target mode (null
    /// otherwise).
    const Texture3DDesc& volumeDesc() const noexcept
    {
        return m_volume_desc.volume;
    }
    TextureView* volumeView() const noexcept { return m_volume_view; }
    void setVolumeView(TextureView* view) { m_volume_view = view; }
    /// The volume's view in layer-target mode (the borrowed attachment).
    MtlTextureView* borrowedVolumeView() { return &m_borrowed_view; }
    std::uint32_t borrowedLayer() const noexcept
    {
        return m_volume_desc.layer;
    }
    bool borrowsVolume() const noexcept { return m_borrows_volume; }
    bool released = false;
    /// Marks the handle released (the backend releases the device objects
    /// but keeps the handle alive, so use-after-release is detectable).
    void release() { released = true; }
private:
    RenderTargetDesc m_desc;
    VolumeLayerDesc m_volume_desc;
    TextureView* m_volume_view = nullptr;
    std::vector<MtlTextureView> m_color_views;
    MtlTextureView m_depth_view;
    MtlTextureView m_borrowed_view;
    bool m_borrows_volume = false;
};
/// 3D texture of the Metal backend: one MTLTexture with the
/// Type3D type. The backend creates and fills the texture; the view
/// reports the volume's own layer count (milestone 7).
class MtlTexture3DView final : public TextureView
{
public:
    MtlTexture3DView(const Texture3DDesc& desc) : m_desc(desc) {}
    std::uint32_t width() const noexcept override { return m_desc.width; }
    std::uint32_t height() const noexcept override { return m_desc.height; }
    std::uint32_t depth() const noexcept override { return m_desc.depth; }
    PixelFormat format() const noexcept override { return m_desc.format; }
    bool isDepth() const noexcept override { return false; }
    std::size_t sliceBytes() const noexcept
    {
        return static_cast<std::size_t>(m_desc.width) * m_desc.height
            * bytesPerPixel(m_desc.format);
    }
    const Texture3DDesc& desc() const noexcept { return m_desc; }
    /// The volume's device-side MTLTexture (the backend owns it; the fill
    /// and read paths go through this handle). Null until the backend
    /// created the texture.
    MTLTextureHandle texture() const noexcept { return m_texture; }
    void setTexture(MTLTextureHandle texture) { m_texture = texture; }
    bool released = false;
private:
    Texture3DDesc m_desc;
    MTLTextureHandle m_texture = nullptr;
};

/// 2D texture of the Metal backend (milestone 23): one MTLTexture with
/// the Type2D type. The backend creates and fills the texture; the same
/// backend-owned registry the volume's one-layer form, the same
/// staging blit path, the same byte counting.
class MtlTexture2DView final : public TextureView
{
public:
    MtlTexture2DView(const Texture2DDesc& desc) : m_desc(desc) {}
    std::uint32_t width() const noexcept override { return m_desc.width; }
    std::uint32_t height() const noexcept override { return m_desc.height; }
    std::uint32_t depth() const noexcept override { return 1u; }
    PixelFormat format() const noexcept override { return m_desc.format; }
    bool isDepth() const noexcept override { return false; }
    std::size_t textureBytes() const noexcept
    {
        return static_cast<std::size_t>(m_desc.width) * m_desc.height
            * bytesPerPixel(m_desc.format);
    }
    const Texture2DDesc& desc() const noexcept { return m_desc; }
    /// The texture's device-side MTLTexture (the backend owns it; the
    /// fill and read paths go through this handle). Null until the
    /// backend created the texture.
    MTLTextureHandle texture() const noexcept { return m_texture; }
    void setTexture(MTLTextureHandle texture) { m_texture = texture; }
    bool released = false;
private:
    Texture2DDesc m_desc;
    MTLTextureHandle m_texture = nullptr;
};
}  // namespace metal
}  // namespace render
}  // namespace grace
