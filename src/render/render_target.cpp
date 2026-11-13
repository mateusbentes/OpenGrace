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

void validateTexture2DDesc(const Texture2DDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
        throw std::invalid_argument("texture 2D: width and height must be positive");
    if (desc.format == PixelFormat::Depth24Stencil8)
        throw std::invalid_argument(
            "texture 2D: Depth24Stencil8 is an attachment format and cannot describe a sampled "
            "texture; the driver rejects the sampled form with GL_INVALID_OPERATION");
}

void validateVolumeLayerDesc(const VolumeLayerDesc& desc)
{
    validateTexture3DDesc(desc.volume);
    if (desc.layer >= desc.volume.depth)
        throw std::invalid_argument(
            "volume layer: the layer index is out of range for the volume's depth");
}

}  // namespace grace::render
