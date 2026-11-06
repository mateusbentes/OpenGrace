#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "grace/render/render_backend.hpp"

namespace grace::render {

/// Buffer selection for clear(), as a platform-neutral bitmask.
enum class ClearBuffer : std::uint32_t
{
    Color = 1u << 0,
    Depth = 1u << 1,
    Stencil = 1u << 2
};

constexpr ClearBuffer operator|(ClearBuffer lhs, ClearBuffer rhs) noexcept
{
    return static_cast<ClearBuffer>(static_cast<std::uint32_t>(lhs) |
                                    static_cast<std::uint32_t>(rhs));
}

/// Configuration of the OpenGL backend. Plain data only: no SDL, OpenGL or
/// window types appear in this header.
struct OpenGLBackendConfig
{
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool hidden = true;            ///< Create the window hidden (for Xvfb runs).
    bool readback_center = false;  ///< Read back the centre pixel before the swap.
    bool draw_triangle = false;    ///< Draw the diagnostic triangle each frame.
    ClearColor triangle_color{200, 60, 40, 255};
    std::uint32_t clear_buffers = static_cast<std::uint32_t>(
        ClearBuffer::Color | ClearBuffer::Depth | ClearBuffer::Stencil);
    float clear_depth = 1.0f;
    std::int32_t clear_stencil = 0;
    int swap_interval = 1;
};

/// One pixel sample captured before a buffer swap.
struct ReadbackSample
{
    std::uint32_t x;
    std::uint32_t y;
    ClearColor color;
    float depth;
    std::int32_t stencil;
};

/// OpenGL render backend behind the platform-neutral RenderBackend contract.
///
/// SDL and OpenGL types are hidden behind a pimpl. Context creation happens in
/// initialise(); mutating contract calls made before initialise() or after
/// shutdown() are counted as protocol violations. shutdown() is safe to call
/// twice (the second call is counted as a violation but is a no-op).
class OpenGLRenderBackend final : public RenderBackend
{
public:
    /// Throws std::invalid_argument when width or height is zero.
    explicit OpenGLRenderBackend(OpenGLBackendConfig config);
    ~OpenGLRenderBackend() override;

    OpenGLRenderBackend(const OpenGLRenderBackend&) = delete;
    OpenGLRenderBackend& operator=(const OpenGLRenderBackend&) = delete;

    /// Creates the window and the GL context (3.3 core profile first, then a
    /// 3.0 fallback without a profile flag, recording which one was obtained),
    /// loads the GL entry points and queries the context strings. Throws
    /// std::runtime_error with a clear message when no context can be created
    /// or a symbol cannot be resolved.
    void initialise();

    // -- RenderBackend contract --------------------------------------------
    std::string describe() const override;
    void setClearColor(ClearColor color) override;
    ClearColor clearColor() const override;
    void clear() override;
    std::uint32_t width() const override;
    std::uint32_t height() const override;
    void shutdown() override;

    // -- Diagnostics (not part of the contract) ----------------------------
    std::uint64_t violationCount() const noexcept;
    std::uint64_t glErrorCount() const noexcept;
    std::string lastError() const noexcept;

    bool hasCenterPixel() const noexcept;
    ClearColor centerPixel() const noexcept;
    float centerDepth() const noexcept;
    std::int32_t centerStencil() const noexcept;

    /// Queues a pixel to be read back before the next buffer swap. The back
    /// buffer contents are undefined after a swap, so all readback happens
    /// pre-swap. Requires initialise().
    void queueReadback(std::uint32_t x, std::uint32_t y);

    /// Samples captured before the last swap, in queue order.
    const std::vector<ReadbackSample>& lastReadback() const noexcept;

    void setClearBuffers(std::uint32_t mask) noexcept;
    void setClearDepth(float depth) noexcept;
    void setClearStencil(std::int32_t stencil) noexcept;

    int contextMajor() const noexcept;
    int contextMinor() const noexcept;
    bool usedFallbackContext() const noexcept;
    std::string glVersionString() const noexcept;
    std::string glVendor() const noexcept;
    std::string glRenderer() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void shutdownImpl(bool countViolation);
    void ensureTriangle();
    void drawTriangle();
    unsigned int compileShader(bool vertex, const char* source);
    unsigned int linkProgram(unsigned int vertexShader, unsigned int fragmentShader);
    void readCenter();
    void captureQueuedReadbacks();
    ClearColor readPixel(std::uint32_t x, std::uint32_t y);
    float readDepth(std::uint32_t x, std::uint32_t y);
    std::int32_t readStencilAt(std::uint32_t x, std::uint32_t y);
};

}  // namespace grace::render
