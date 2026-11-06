#pragma once

#include <GL/gl.h>

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

}  // namespace grace::render::opengl
