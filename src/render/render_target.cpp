#include "grace/render/render_target.hpp"

#include <stdexcept>

namespace grace::render {

void validateRenderTargetDesc(const RenderTargetDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
        throw std::invalid_argument("render target: width and height must be positive");
    if (!desc.colorAttachment && !desc.depthAttachment)
        throw std::invalid_argument(
            "render target: incomplete (neither a colour nor a depth attachment)");
}

}  // namespace grace::render
