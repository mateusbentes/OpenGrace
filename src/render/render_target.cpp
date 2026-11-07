#include "grace/render/render_target.hpp"

#include <stdexcept>

namespace grace::render {

void validateRenderTargetDesc(const RenderTargetDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
        throw std::invalid_argument("render target: width and height must be positive");
    if (desc.colorAttachmentCount > kMaxColorAttachments)
        throw std::invalid_argument("render target: colorAttachmentCount exceeds the documented "
                                    "hard cap of kMaxColorAttachments (4)");
    if (desc.colorAttachmentCount == 0 && !desc.depthAttachment)
        throw std::invalid_argument(
            "render target: incomplete (neither a colour nor a depth attachment)");
}

void validateTexture3DDesc(const Texture3DDesc& desc)
{
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
        throw std::invalid_argument(
            "texture 3D: width, height and depth must all be positive");
    if (desc.format == PixelFormat::Depth24Stencil8)
        throw std::invalid_argument(
            "texture 3D: Depth24Stencil8 is a 2D attachment format and cannot describe a "
            "volume; the driver rejects it with GL_INVALID_OPERATION");
}

}  // namespace grace::render
