#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "grace/render/render_backend.hpp"

namespace grace::render {

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
    void setDepthTestEnabled(bool enabled) override;
    void clear() override;
    std::uint32_t width() const override;
    std::uint32_t height() const override;
    void shutdown() override;
    std::uint64_t violationCount() const noexcept override;

    std::unique_ptr<ShaderProgram> createShaderProgram(ShaderProgramId id) override;
    std::unique_ptr<GpuMesh> createGpuMesh() override;
    void draw(GpuMesh& mesh, ShaderProgram& program) override;
    void submit(const DrawSubmission& submission) override;

    RenderTarget* createRenderTarget(const RenderTargetDesc& desc) override;
    void beginRenderPass(RenderTarget* target) override;
    void endRenderPass() override;
    void releaseRenderTarget(RenderTarget* target) override;
    TextureView* renderTargetTexture(RenderTarget* target) override;
    TextureView* renderTargetTexture(RenderTarget* target, std::uint32_t attachment) override;

    void setCamera(const CameraParams& camera) override;
    CameraParams camera() const override;
    void bindTexture(const TextureView* view, std::uint32_t unit) override;
    TextureView* createTexture3D(const Texture3DDesc& desc) override;
    void releaseTexture3D(TextureView* view) override;
    void bindTexture3D(const TextureView* view, std::uint32_t unit) override;
    void fillTexture3D(TextureView* view, std::uint32_t layer, const void* data,
                       std::size_t sizeBytes) override;
    bool readTexture3D(const TextureView* view, std::uint32_t layer, void* data,
                       std::size_t sizeBytes) override;
    RenderTarget* createVolumeLayerTarget(TextureView* volume, std::uint32_t layer) override;
    void copyDepthAttachment(RenderTarget* source, RenderTarget* destination) override;

    /// Queues a pixel to be read back before the next buffer swap. The back
    /// buffer contents are undefined after a swap, so all readback happens
    /// pre-swap.
    void queueReadback(std::uint32_t x, std::uint32_t y) override;

    /// Reads the centre pixel and all queued pixels from the current read
    /// buffer immediately (valid until the next swap). clear() calls this
    /// automatically when readback_center is set or pixels are queued.
    void readback() override;
    void swapBuffers() override;

    const std::vector<ReadbackSample>& lastReadback() const noexcept override;
    bool hasCenterPixel() const noexcept override;
    ClearColor centerPixel() const noexcept override;
    float centerDepth() const noexcept override;
    std::int32_t centerStencil() const noexcept override;

    // -- OpenGL-specific diagnostics and configuration (not in the contract)
    void setClearBuffers(std::uint32_t mask) noexcept;
    void setClearDepth(float depth) noexcept;
    void setClearStencil(std::int32_t stencil) noexcept;

    std::uint64_t glErrorCount() const noexcept;
    std::string lastError() const noexcept;

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
    void ensureLightingProgram();
    void submitImpl(const DrawSubmission& submission);
    void applyViewport();
    void applyDepthState();
    void readCenter();
    void captureQueuedReadbacks();
    ClearColor readPixel(std::uint32_t x, std::uint32_t y);
    float readDepth(std::uint32_t x, std::uint32_t y);
    std::int32_t readStencilAt(std::uint32_t x, std::uint32_t y);
};

}  // namespace grace::render
