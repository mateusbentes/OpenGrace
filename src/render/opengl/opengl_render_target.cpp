#include "opengl_render_target.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace grace::render::opengl {

namespace {

GLint toInternalFormat(PixelFormat format)
{
    switch (format)
    {
    case PixelFormat::Rgba8:
        return GL_RGBA8;
    case PixelFormat::Rgba16F:
        return GL_RGBA16F;
    case PixelFormat::R8:
        return GL_R8;
    case PixelFormat::Depth24Stencil8:
        return GL_DEPTH24_STENCIL8;
    }
    return GL_RGBA8;
}

GLenum toPixelFormat(PixelFormat format)
{
    switch (format)
    {
    case PixelFormat::Depth24Stencil8:
        return GL_DEPTH_STENCIL;
    case PixelFormat::R8:
        return GL_RED;
    default:
        return GL_RGBA;
    }
}

GLenum toPixelType(PixelFormat format)
{
    switch (format)
    {
    case PixelFormat::Rgba8:
        return GL_UNSIGNED_BYTE;
    case PixelFormat::Rgba16F:
        return GL_HALF_FLOAT;
    case PixelFormat::R8:
        return GL_UNSIGNED_BYTE;
    case PixelFormat::Depth24Stencil8:
        return GL_UNSIGNED_INT_24_8;
    }
    return GL_UNSIGNED_BYTE;
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

// Layer-target mode: the FBO's only colour attachment is layer z of an existing
// volume. The texture is borrowed, so it is never added to m_color_textures and
// destroyGlObjects() never deletes it: releasing this target must leave the
// volume and its contents intact, which is what makes a per-layer loop safe.
GlRenderTarget::GlRenderTarget(GlLoader& gl, GLuint volumeTexture, const VolumeLayerDesc& desc)
    : m_gl(gl)
{
    validateVolumeLayerDesc(desc);  // throws on a bad volume or layer
    borrows_volume = true;
    m_desc.width = desc.volume.width;
    m_desc.height = desc.volume.height;
    m_desc.colorAttachmentCount = 1;
    m_desc.colorFormats[0] = desc.volume.format;
    m_desc.depthAttachment = false;

    m_gl.glGenFramebuffers(1, &m_fbo);
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl.glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, volumeTexture, 0,
                                   static_cast<GLint>(desc.layer));
    m_gl.glDrawBuffer(GL_COLOR_ATTACHMENT0);
    m_gl.glReadBuffer(GL_COLOR_ATTACHMENT0);
    // The view exposes the borrowed layer for sampling. Its width and height are
    // the volume's and its depth() stays 1, because this view is one layer of a
    // volume and not the volume itself.
    m_color_views.emplace_back(volumeTexture, desc.volume.width, desc.volume.height,
                               desc.volume.format, false);

    const GLenum status = m_gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    m_gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "0x%04X", static_cast<unsigned int>(status));
        destroyGlObjects();
        throw std::runtime_error(std::string("volume layer target incomplete, status ") + buffer);
    }
}

void GlRenderTarget::destroyGlObjects()
{
    // A layer target's colour texture belongs to the volume, not to this target.
    if (!borrows_volume)
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

// --- GlTexture3DView -------------------------------------------------------

GlTexture3DView::GlTexture3DView(GlLoader& gl, const Texture3DDesc& desc)
    : m_gl(gl), m_desc(desc)
{
    m_gl.glGenTextures(1, &m_texture);
    m_gl.glBindTexture(GL_TEXTURE_3D, m_texture);
    m_gl.glTexImage3D(GL_TEXTURE_3D, 0, toInternalFormat(m_desc.format),
                      static_cast<GLsizei>(m_desc.width), static_cast<GLsizei>(m_desc.height),
                      static_cast<GLsizei>(m_desc.depth), 0, toPixelFormat(m_desc.format),
                      toPixelType(m_desc.format), nullptr);
    m_gl.glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl.glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl.glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl.glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_gl.glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

GlTexture3DView::~GlTexture3DView()
{
    if (m_texture != 0)
    {
        GLuint texture = m_texture;
        m_gl.glDeleteTextures(1, &texture);
        m_texture = 0;
    }
}

void GlTexture3DView::uploadSlice(std::uint32_t layer, const void* data, std::size_t sizeBytes)
{
    if (layer >= m_desc.depth || sizeBytes != sliceBytes())
        throw std::invalid_argument(
            "GlTexture3DView::uploadSlice: layer out of range or byte count does not match");
    // Row alignment 1. With the default of 4, a single-channel slice whose row
    // length is not a multiple of four bytes is read with the wrong stride.
    // Measured on this driver: an R8 3x3x2 volume round-trips exactly with
    // alignment 1 and does not with alignment 4.
    m_gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    m_gl.glBindTexture(GL_TEXTURE_3D, m_texture);
    m_gl.glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, static_cast<GLint>(layer),
                         static_cast<GLsizei>(m_desc.width),
                         static_cast<GLsizei>(m_desc.height), 1, toPixelFormat(m_desc.format),
                         toPixelType(m_desc.format), data);
    m_gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void GlTexture3DView::readSlice(std::uint32_t layer, void* data, std::size_t sizeBytes) const
{
    if (layer >= m_desc.depth || sizeBytes != sliceBytes())
        throw std::invalid_argument(
            "GlTexture3DView::readSlice: layer out of range or byte count does not match");
    // glGetTexImage takes no layer: it reads a whole mip level. The volume is
    // bound to a scratch unit, the whole level is read into a buffer, and only
    // the requested layer is copied out. The active unit and its previous 3D
    // binding are restored before returning, so sampling state is untouched.
    constexpr GLenum kScratchUnit = GL_TEXTURE0 + 15;
    GLint previous_unit = GL_TEXTURE0;
    GLint previous_texture = 0;
    m_gl.glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_unit);
    m_gl.glActiveTexture(kScratchUnit);
    m_gl.glGetIntegerv(GL_TEXTURE_BINDING_3D, &previous_texture);
    m_gl.glBindTexture(GL_TEXTURE_3D, m_texture);
    m_gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);

    std::vector<std::byte> whole(sliceBytes() * static_cast<std::size_t>(m_desc.depth));
    m_gl.glGetTexImage(GL_TEXTURE_3D, 0, toPixelFormat(m_desc.format), toPixelType(m_desc.format),
                       whole.data());

    m_gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
    m_gl.glBindTexture(GL_TEXTURE_3D, static_cast<GLuint>(previous_texture));
    m_gl.glActiveTexture(static_cast<GLenum>(previous_unit));

    const std::size_t offset = static_cast<std::size_t>(layer) * sliceBytes();
    std::memcpy(data, whole.data() + offset, sizeBytes);
}

}  // namespace grace::render::opengl
