#pragma once

#include <GL/glcorearb.h>
#undef near
#undef far

#include <cstddef>
#include <cstdint>
#include <vector>

#include "grace/render/opengl/gl_loader.hpp"
#include "grace/render/render_target.hpp"

namespace grace::render::opengl {

/// Texture view of one attachment of a GlRenderTarget.
class GlTextureView final : public TextureView
{
public:
    GlTextureView() = default;
    GlTextureView(GLuint texture, std::uint32_t width, std::uint32_t height,
                  PixelFormat format, bool isDepth)
        : m_texture(texture), m_width(width), m_height(height), m_format(format),
          m_is_depth(isDepth)
    {
    }

    std::uint32_t width() const noexcept override { return m_width; }
    std::uint32_t height() const noexcept override { return m_height; }
    PixelFormat format() const noexcept override { return m_format; }
    bool isDepth() const noexcept override { return m_is_depth; }

    GLuint textureId() const noexcept { return m_texture; }

private:
    GLuint m_texture = 0;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    PixelFormat m_format = PixelFormat::Rgba8;
    bool m_is_depth = false;
};

/// OpenGL render target: FBO with up to kMaxColorAttachments colour texture
/// attachments plus an optional depth/stencil attachment. The project
/// PixelFormat values are mapped onto GL constants here, inside the OpenGL
/// module only. Draw buffers are set once at creation (FBO state). Throws
/// std::runtime_error when the FBO is incomplete.
class GlRenderTarget final : public RenderTarget
{
public:
    GlRenderTarget(GlLoader& gl, const RenderTargetDesc& desc);
    /// Layer-target mode (milestone 7, second increment): an FBO whose single
    /// colour attachment is one layer of an existing 3D texture, attached with
    /// glFramebufferTextureLayer. The target borrows the texture and never
    /// deletes it, so releasing the target leaves the volume intact. Throws
    /// std::runtime_error when the FBO is incomplete, like the 2D constructor.
    GlRenderTarget(GlLoader& gl, GLuint volumeTexture, const VolumeLayerDesc& desc);
    ~GlRenderTarget() override { destroyGlObjects(); }

    GlRenderTarget(const GlRenderTarget&) = delete;
    GlRenderTarget& operator=(const GlRenderTarget&) = delete;

    std::uint32_t width() const noexcept override { return m_desc.width; }
    std::uint32_t height() const noexcept override { return m_desc.height; }
    std::uint32_t colorAttachmentCount() const noexcept override
    {
        return m_desc.colorAttachmentCount;
    }
    bool hasColorAttachment() const noexcept override { return m_desc.colorAttachmentCount > 0; }
    bool hasDepthAttachment() const noexcept override { return m_desc.depthAttachment; }

    GLuint framebufferId() const noexcept { return m_fbo; }
    TextureView* colorView(std::uint32_t index) noexcept
    {
        return index < m_color_views.size() ? &m_color_views[index] : nullptr;
    }
    TextureView* depthView() noexcept
    {
        return m_desc.depthAttachment ? &m_depth_view : nullptr;
    }

    bool released = false;
    /// True in layer-target mode: the colour attachment is a borrowed volume
    /// layer, so destroyGlObjects() must not delete that texture.
    bool borrows_volume = false;

    /// Deletes the GL objects but keeps the handle object alive (the backend
    /// registry still owns it), so use-after-release is detectable.
    void release();

private:
    void destroyGlObjects();

    GlLoader& m_gl;
    RenderTargetDesc m_desc;
    GLuint m_fbo = 0;
    std::vector<GLuint> m_color_textures;
    std::vector<GlTextureView> m_color_views;
    GLuint m_depth_texture = 0;
    GlTextureView m_depth_view;
};

/// 3D texture of the OpenGL backend: one GL_TEXTURE_3D object, created with
/// glTexImage3D and filled per slice with glTexSubImage3D (milestone 7, first
/// increment). It owns no framebuffer: the contract has no pass that renders
/// into a volume yet, and the round trip is what the capability is proved by.
class GlTexture3DView final : public TextureView
{
public:
    GlTexture3DView(GlLoader& gl, const Texture3DDesc& desc);
    ~GlTexture3DView() override;

    GlTexture3DView(const GlTexture3DView&) = delete;
    GlTexture3DView& operator=(const GlTexture3DView&) = delete;

    std::uint32_t width() const noexcept override { return m_desc.width; }
    std::uint32_t height() const noexcept override { return m_desc.height; }
    std::uint32_t depth() const noexcept override { return m_desc.depth; }
    PixelFormat format() const noexcept override { return m_desc.format; }
    bool isDepth() const noexcept override { return false; }

    GLuint textureId() const noexcept { return m_texture; }
    std::size_t sliceBytes() const noexcept
    {
        return static_cast<std::size_t>(m_desc.width) * m_desc.height * bytesPerPixel(m_desc.format);
    }

    /// Replaces one slice. Throws std::invalid_argument on a bad layer or size,
    /// the same rejection the null backend makes, and std::runtime_error when
    /// the driver reports an error.
    void uploadSlice(std::uint32_t layer, const void* data, std::size_t sizeBytes);

    /// Reads one slice back through glGetTexImage. The volume is bound to a
    /// scratch texture unit first, so the unit the caller had selected is
    /// restored afterwards and the call does not disturb sampling state.
    void readSlice(std::uint32_t layer, void* data, std::size_t sizeBytes) const;

private:
    GlLoader& m_gl;
    Texture3DDesc m_desc;
    GLuint m_texture = 0;
};

}  // namespace grace::render::opengl
