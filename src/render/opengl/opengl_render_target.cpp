#include "opengl_render_target.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace grace::render::opengl {

namespace {

GLint toInternalFormat(PixelFormat format)
{
    return format == PixelFormat::Rgba8 ? GL_RGBA8 : GL_DEPTH24_STENCIL8;
}

GLenum toPixelFormat(PixelFormat format)
{
    return format == PixelFormat::Rgba8 ? GL_RGBA : GL_DEPTH_STENCIL;
}

GLenum toPixelType(PixelFormat format)
{
    return format == PixelFormat::Rgba8 ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT_24_8;
}

GLenum toAttachment(PixelFormat format)
{
    return format == PixelFormat::Rgba8 ? GL_COLOR_ATTACHMENT0 : GL_DEPTH_STENCIL_ATTACHMENT;
}

}  // namespace

GlRenderTarget::GlRenderTarget(GlLoader& gl, const RenderTargetDesc& desc)
    : m_gl(gl),
      m_desc(desc),
      m_color_view(0, desc.width, desc.height, desc.colorFormat, false),
      m_depth_view(0, desc.width, desc.height, desc.depthFormat, true)
{
    validateRenderTargetDesc(desc);

    m_gl.glGenFramebuffers(1, &m_fbo);
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    if (desc.colorAttachment)
    {
        m_gl.glGenTextures(1, &m_color_texture);
        m_gl.glBindTexture(GL_TEXTURE_2D, m_color_texture);
        m_gl.glTexImage2D(GL_TEXTURE_2D, 0, toInternalFormat(desc.colorFormat),
                          static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 0,
                          toPixelFormat(desc.colorFormat), toPixelType(desc.colorFormat),
                          nullptr);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                    m_color_texture, 0);
    }

    if (desc.depthAttachment)
    {
        m_gl.glGenTextures(1, &m_depth_texture);
        m_gl.glBindTexture(GL_TEXTURE_2D, m_depth_texture);
        m_gl.glTexImage2D(GL_TEXTURE_2D, 0, toInternalFormat(desc.depthFormat),
                          static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 0,
                          toPixelFormat(desc.depthFormat), toPixelType(desc.depthFormat),
                          nullptr);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, toAttachment(desc.depthFormat),
                                    GL_TEXTURE_2D, m_depth_texture, 0);
    }

    const GLenum status = m_gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "0x%04X", static_cast<unsigned int>(status));
        destroyGlObjects();
        throw std::runtime_error(std::string("render target incomplete, status ") + buffer);
    }

    // Point the views at the real texture ids now that they exist.
    m_color_view = GlTextureView(m_color_texture, desc.width, desc.height, desc.colorFormat, false);
    m_depth_view = GlTextureView(m_depth_texture, desc.width, desc.height, desc.depthFormat, true);
}

void GlRenderTarget::release()
{
    released = true;
    destroyGlObjects();
}

void GlRenderTarget::destroyGlObjects()
{
    if (m_color_texture != 0)
    {
        m_gl.glDeleteTextures(1, &m_color_texture);
        m_color_texture = 0;
    }
    if (m_depth_texture != 0)
    {
        m_gl.glDeleteTextures(1, &m_depth_texture);
        m_depth_texture = 0;
    }
    if (m_fbo != 0)
    {
        m_gl.glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
}

}  // namespace grace::render::opengl
