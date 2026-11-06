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

GLenum toColorAttachment(std::uint32_t index)
{
    return GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index);
}

}  // namespace

GlRenderTarget::GlRenderTarget(GlLoader& gl, const RenderTargetDesc& desc)
    : m_gl(gl), m_desc(desc)
{
    validateRenderTargetDesc(desc);  // throws, enforces the attachment cap

    m_gl.glGenFramebuffers(1, &m_fbo);
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    m_color_textures.resize(desc.colorAttachmentCount);
    m_color_views.reserve(desc.colorAttachmentCount);
    for (std::uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const PixelFormat format = desc.colorFormats[i];
        GLuint tex = 0;
        m_gl.glGenTextures(1, &tex);
        m_gl.glBindTexture(GL_TEXTURE_2D, tex);
        m_gl.glTexImage2D(GL_TEXTURE_2D, 0, toInternalFormat(format),
                          static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 0,
                          toPixelFormat(format), toPixelType(format), nullptr);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, toColorAttachment(i), GL_TEXTURE_2D, tex, 0);
        m_color_textures[i] = tex;
        m_color_views.emplace_back(tex, desc.width, desc.height, format, false);
    }

    if (desc.colorAttachmentCount > 0)
    {
        GLenum buffers[kMaxColorAttachments];
        for (std::uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
            buffers[i] = toColorAttachment(i);
        m_gl.glDrawBuffers(static_cast<GLsizei>(desc.colorAttachmentCount), buffers);
        m_gl.glReadBuffer(toColorAttachment(0));
    }
    else
    {
        // Depth-only target: no colour buffer.
        m_gl.glDrawBuffer(GL_NONE);
        m_gl.glReadBuffer(GL_NONE);
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
        m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                    m_depth_texture, 0);
        m_depth_view = GlTextureView(m_depth_texture, desc.width, desc.height, desc.depthFormat,
                                     true);
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
}

void GlRenderTarget::release()
{
    released = true;
    destroyGlObjects();
}

void GlRenderTarget::destroyGlObjects()
{
    for (GLuint tex : m_color_textures)
        if (tex != 0)
        {
            GLuint t = tex;
            m_gl.glDeleteTextures(1, &t);
        }
    m_color_textures.clear();
    m_color_views.clear();
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
