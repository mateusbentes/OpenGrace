#pragma once

#include <GL/gl.h>

#include "grace/render/opengl/gl_loader.hpp"
#include "grace/render/render_target.hpp"

namespace grace::render::opengl {

/// Texture view of one attachment of a GlRenderTarget.
class GlTextureView final : public TextureView
{
public:
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
    GLuint m_texture;
    std::uint32_t m_width;
    std::uint32_t m_height;
    PixelFormat m_format;
    bool m_is_depth;
};

/// OpenGL render target: FBO plus colour/depth texture attachments. The
/// project PixelFormat values are mapped onto GL constants here, inside the
/// OpenGL module only. Throws std::runtime_error when the FBO is incomplete.
class GlRenderTarget final : public RenderTarget
{
public:
    GlRenderTarget(GlLoader& gl, const RenderTargetDesc& desc);
    ~GlRenderTarget() override { destroyGlObjects(); }

    GlRenderTarget(const GlRenderTarget&) = delete;
    GlRenderTarget& operator=(const GlRenderTarget&) = delete;

    std::uint32_t width() const noexcept override { return m_desc.width; }
    std::uint32_t height() const noexcept override { return m_desc.height; }
    bool hasColorAttachment() const noexcept override { return m_desc.colorAttachment; }
    bool hasDepthAttachment() const noexcept override { return m_desc.depthAttachment; }

    GLuint framebufferId() const noexcept { return m_fbo; }
    TextureView* colorView() noexcept { return m_desc.colorAttachment ? &m_color_view : nullptr; }
    TextureView* depthView() noexcept { return m_desc.depthAttachment ? &m_depth_view : nullptr; }

    bool released = false;

    /// Deletes the GL objects but keeps the handle object alive (the backend
    /// registry still owns it), so use-after-release is detectable.
    void release();

private:
    void destroyGlObjects();

    GlLoader& m_gl;
    RenderTargetDesc m_desc;
    GLuint m_fbo = 0;
    GLuint m_color_texture = 0;
    GLuint m_depth_texture = 0;
    GlTextureView m_color_view{0, 0, 0, PixelFormat::Rgba8, false};
    GlTextureView m_depth_view{0, 0, 0, PixelFormat::Depth24Stencil8, true};
};

}  // namespace grace::render::opengl
