#pragma once

#include "grace/render/vulkan_render_backend.hpp"

#include <memory>

namespace grace::render {

/// The internal header is intentionally empty: the Vulkan types are private
/// to the backend's translation unit (the same split the OpenGL backend
/// uses), and the pimpl key is defined in vulkan_render_backend.cpp.

}  // namespace grace::render
