#include "metal_render_target.hpp"
namespace grace {
namespace render {
namespace metal {
// The Metal render target is a plain data handle: the backend owns the
// MTLTextures, and this module's translation unit exists so the handle
// types have a home (the same split the OpenGL and the Vulkan backends
// use).
//
// released: the handle stays addressable after releaseRenderTarget so the
// use-after-release is detected by the backend's violation counter, not
// by an invalid pointer.
}  // namespace metal
}  // namespace render
}  // namespace grace
