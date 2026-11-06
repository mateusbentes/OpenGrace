#pragma once

#include <cstdint>
#include <string>

namespace grace::render {

/// Straight (non-premultiplied) RGBA colour, 8 bits per channel.
struct ClearColor
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

constexpr bool operator==(const ClearColor& lhs, const ClearColor& rhs) noexcept
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

constexpr bool operator!=(const ClearColor& lhs, const ClearColor& rhs) noexcept
{
    return !(lhs == rhs);
}

/// Platform-neutral rendering contract.
///
/// This header must stay free of any platform, windowing or graphics-API
/// types: no OpenGL, SDL, Vulkan, Metal, Direct3D or window handles may
/// appear here. Concrete backends (null, OpenGL) implement this interface in
/// their own modules.
class RenderBackend
{
public:
    virtual ~RenderBackend() = default;

    /// Human-readable backend description, for example "null (headless)".
    virtual std::string describe() const = 0;

    /// Sets the colour used by subsequent clear() calls.
    virtual void setClearColor(ClearColor color) = 0;

    /// The colour currently used by clear().
    virtual ClearColor clearColor() const = 0;

    /// Clears the frame target with the current clear colour.
    virtual void clear() = 0;

    /// Frame target size in pixels.
    virtual std::uint32_t width() const = 0;
    virtual std::uint32_t height() const = 0;

    /// Releases backend resources. Exactly one call per backend lifetime;
    /// calls after shutdown are protocol violations.
    virtual void shutdown() = 0;
};

}  // namespace grace::render
