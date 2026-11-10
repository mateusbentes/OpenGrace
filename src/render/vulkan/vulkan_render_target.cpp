#include "vulkan_render_target.hpp"

namespace grace::render::vulkan {

// The Vulkan render target is a plain data handle: the backend owns the
// VkImages, the VkImageViews, the VkRenderPass and the VkFramebuffer, and
// this module's translation unit exists so the handle types have a home
// (the same split the OpenGL backend uses).
//
// released: the handle stays addressable after releaseRenderTarget so the
// use-after-release is detected by the backend's violation counter, not by
// an invalid pointer.

}  // namespace grace::render::vulkan
