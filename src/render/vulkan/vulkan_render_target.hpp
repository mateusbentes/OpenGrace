#pragma once

#include <cstdint>
#include <vector>

#include "grace/render/render_target.hpp"
#include "vk_loader.hpp"

namespace grace::render::vulkan {

/// Texture view of one attachment of a VkRenderTarget (or of a volume). The
/// backend creates and destroys the underlying VkImageView; the view handle
/// itself is plain data the engine holds through the contract.
class VkTextureView final : public TextureView
{
public:
    VkTextureView() = default;
    VkTextureView(VkImageView view, std::uint32_t width, std::uint32_t height,
                  PixelFormat format, bool isDepth, std::uint32_t depth = 1)
        : m_view(view), m_width(width), m_height(height), m_format(format),
          m_is_depth(isDepth), m_depth(depth)
    {
    }

    std::uint32_t width() const noexcept override { return m_width; }
    std::uint32_t height() const noexcept override { return m_height; }
    std::uint32_t depth() const noexcept override { return m_depth; }
    PixelFormat format() const noexcept override { return m_format; }
    bool isDepth() const noexcept override { return m_is_depth; }

    VkImageView imageView() const noexcept { return m_view; }

private:
    VkImageView m_view = nullptr;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    PixelFormat m_format = PixelFormat::Rgba8;
    bool m_is_depth = false;
    std::uint32_t m_depth = 1;
};

/// Vulkan render target: up to kMaxColorAttachments colour attachments plus an
/// optional depth/stencil attachment. The backend creates the VkImages, the
/// VkImageViews, the VkRenderPass (per attachment-format combination) and the
/// VkFramebuffer; this handle holds them and the contract-visible views. The
/// project PixelFormat values are mapped onto Vulkan formats inside the Vulkan
/// module only.
class VkRenderTarget final : public RenderTarget
{
public:
    VkRenderTarget(const RenderTargetDesc& desc)
        : m_desc(desc), m_color_views(desc.colorAttachmentCount)
    {
    }
    /// Layer-target mode (milestone 7, second increment): the single colour
    /// attachment is one layer of an existing volume. The target borrows the
    /// volume's image and never destroys it, so releasing the target leaves
    /// the volume intact — the same ownership rule the OpenGL backend states.
    VkRenderTarget(const VolumeLayerDesc& desc)
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
        return index < m_color_views.size() ? &m_color_views[index] : nullptr;
    }
    TextureView* depthView()
    {
        return m_desc.depthAttachment ? &m_depth_view : nullptr;
    }
    /// The backend fills the contract-facing views when it creates the
    /// device objects (each view reports its attachment's format and kind).
    void setColorView(std::uint32_t index, VkImageView view,
                      std::uint32_t width, std::uint32_t height,
                      PixelFormat format, bool isDepth)
    {
        if (index < m_color_views.size())
        {
            m_color_views[index] =
                VkTextureView(view, width, height, format, isDepth);
        }
    }
    void setDepthView(VkImageView view, std::uint32_t width,
                      std::uint32_t height, PixelFormat format, bool isDepth)
    {
        m_depth_view = VkTextureView(view, width, height, format, isDepth);
    }
    /// The attachment format of the target's colour attachment i (the
    /// project's PixelFormat, mapped onto Vulkan formats by the backend).
    PixelFormat colorFormat(std::uint32_t index) const
    {
        return m_desc.colorFormats[index];
    }
    /// The target's descriptor (the depth view reads its depthFormat).
    const RenderTargetDesc& desc() const noexcept { return m_desc; }
    /// The borrowed volume's image in layer-target mode (null otherwise).
    const Texture3DDesc& volumeDesc() const noexcept
    {
        return m_volume_desc.volume;
    }
    TextureView* volumeView() const noexcept { return m_volume_view; }
    void setVolumeView(TextureView* view) { m_volume_view = view; }
    /// The volume's view in layer-target mode (the borrowed attachment).
    VkTextureView* borrowedVolumeView() { return &m_borrowed_view; }
    std::uint32_t borrowedLayer() const noexcept
    {
        return m_volume_desc.layer;
    }
    bool borrowsVolume() const noexcept { return m_borrows_volume; }

    bool released = false;

    /// Marks the handle released (the backend destroys the device objects but
    /// keeps the handle alive, so use-after-release is detectable).
    void release() { released = true; }

private:
    RenderTargetDesc m_desc;
    VolumeLayerDesc m_volume_desc;
    TextureView* m_volume_view = nullptr;
    std::vector<VkTextureView> m_color_views;
    VkTextureView m_depth_view;
    VkTextureView m_borrowed_view;
    bool m_borrows_volume = false;
};

/// 3D texture of the Vulkan backend: one VkImage with the
/// VK_IMAGE_TYPE_3D type plus a sampler-view. The backend creates and fills
/// the image; the view reports the volume's own layer count (milestone 7).
class VkTexture3DView final : public TextureView
{
public:
    VkTexture3DView(const Texture3DDesc& desc) : m_desc(desc) {}

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
    /// The volume's device-side VkImage (the backend owns it; the fill and
    // read paths go through this handle). Null until the backend created the
    // image.
    VkImage image() const noexcept { return m_image; }
    void setImage(VkImage image) { m_image = image; }
    bool released = false;

private:
    Texture3DDesc m_desc;
    VkImage m_image = nullptr;
};

/// Standalone 2D texture of the Vulkan backend (milestone 23): one
/// VK_IMAGE_TYPE_2D image plus a sampler view. The backend creates and
/// fills the image; the lifetime rule is the volume's one without the
/// layer targets (a 2D texture has no borrowers, so the erase is
/// immediate).
class VkTexture2DView final : public TextureView
{
public:
    VkTexture2DView(const Texture2DDesc& desc) : m_desc(desc) {}

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
    VkImage image() const noexcept { return m_image; }
    void setImage(VkImage image) { m_image = image; }
    bool released = false;

private:
    Texture2DDesc m_desc;
    VkImage m_image = nullptr;
};

}  // namespace grace::render::vulkan
