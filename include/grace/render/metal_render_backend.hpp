#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "grace/render/render_backend.hpp"
namespace grace {
namespace render {
/// Configuration of the Metal backend. Plain data only: no SDL, Metal or
/// window types appear in this header.
struct MetalBackendConfig
{
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    /// Create the window hidden (for headless runs).
    bool hidden = true;
    /// Read back the centre pixel before the swap.
    bool readback_center = false;
    /// Draw the diagnostic triangle each frame.
    bool draw_triangle = false;
    ClearColor triangle_color{200, 60, 40, 255};
    std::uint32_t clear_buffers = static_cast<std::uint32_t>(
        ClearBuffer::Color | ClearBuffer::Depth | ClearBuffer::Stencil);
    float clear_depth = 1.0f;
    std::int32_t clear_stencil = 0;
    int swap_interval = 1;
};
/// Metal render backend behind the platform-neutral RenderBackend contract
/// (milestone 20). macOS and iOS; the loader resolves the Objective-C
/// runtime and the Metal framework at runtime (no link-time dependency,
/// the same rule the OpenGL and the Vulkan backends follow with their
/// loaders), the presentation layer is a CAMetalLayer the SDL window
/// hosts, and the shaders are MSL 2.0 sources compiled at runtime by the
/// device's own MTLLibrary compiler.
///
/// Validation scope (milestone 20's recorded decision): the module
/// compiles on any platform — the loader declares its own types, so no
/// Apple SDK is needed — and the pure mappings (pixel formats, compare
/// ops, blend factors, load/store actions, sampler states, index types)
/// run as unit tests on Linux. The hardware run stays pending until
/// someone builds and runs on a Mac; nothing here claims otherwise.
///
/// Mutating contract calls made before initialise() or after shutdown()
/// are counted as protocol violations. shutdown() is safe to call twice
/// (the second call is counted as a violation but is a no-op).
class MetalRenderBackend final : public RenderBackend
{
public:
    /// Throws std::invalid_argument when width or height is zero.
    explicit MetalRenderBackend(MetalBackendConfig config);
    ~MetalRenderBackend() override;
    MetalRenderBackend(const MetalRenderBackend&) = delete;
    MetalRenderBackend& operator=(const MetalRenderBackend&) = delete;
    /// Creates the window, the Metal device, the command queue and the
    /// presentation layer. Throws std::runtime_error with a clear message
    /// when the platform has no Metal framework, no default device can be
    /// created, or the window system fails.
    void initialise();
    // -- RenderBackend contract --------------------------------------------
    std::string describe() const override;
    void setClearColor(ClearColor color) override;
    ClearColor clearColor() const override;
    void setClearBuffers(std::uint32_t mask) noexcept;
    void setClearDepth(float depth) noexcept;
    void setClearStencil(std::int32_t stencil) noexcept;
    void setDepthTestEnabled(bool enabled) override;
    void clear() override;
    std::uint32_t width() const override;
    std::uint32_t height() const override;
    void shutdown() override;
    std::uint64_t violationCount() const noexcept override;
    std::unique_ptr<ShaderProgram> createShaderProgram(
        ShaderProgramId id) override;
    std::unique_ptr<GpuMesh> createGpuMesh() override;
    void draw(GpuMesh& mesh, ShaderProgram& program) override;
    void submit(const DrawSubmission& submission) override;
    RenderTarget* createRenderTarget(const RenderTargetDesc& desc) override;
    void beginRenderPass(RenderTarget* target) override;
    void endRenderPass() override;
    void releaseRenderTarget(RenderTarget* target) override;
    TextureView* renderTargetTexture(RenderTarget* target) override;
    TextureView* renderTargetTexture(RenderTarget* target,
                                     std::uint32_t attachment) override;
    void setCamera(const CameraParams& camera) override;
    CameraParams camera() const override;
    void bindTexture(const TextureView* view, std::uint32_t unit) override;
    TextureView* createTexture3D(const Texture3DDesc& desc) override;
    void releaseTexture3D(TextureView* view) override;
    void bindTexture3D(const TextureView* view, std::uint32_t unit) override;
    void fillTexture3D(TextureView* view, std::uint32_t layer,
                       const void* data, std::size_t sizeBytes) override;
    bool readTexture3D(const TextureView* view, std::uint32_t layer,
                       void* data, std::size_t sizeBytes) override;
    TextureView* createTexture2D(const Texture2DDesc& desc) override;
    void releaseTexture2D(TextureView* view) override;
    void bindTexture2D(const TextureView* view, std::uint32_t unit) override;
    void fillTexture2D(TextureView* view, const void* data,
                       std::size_t sizeBytes) override;
    bool readTexture2D(const TextureView* view, void* data,
                       std::size_t sizeBytes) override;
    RenderTarget* createVolumeLayerTarget(TextureView* volume,
                                          std::uint32_t layer) override;
    void copyDepthAttachment(RenderTarget* source,
                             RenderTarget* destination) override;
    void queueReadback(std::uint32_t x, std::uint32_t y) override;
    void readback() override;
    void swapBuffers() override;
    const std::vector<ReadbackSample>& lastReadback() const noexcept override;
    bool hasCenterPixel() const noexcept override;
    ClearColor centerPixel() const noexcept override;
    float centerDepth() const noexcept override;
    std::int32_t centerStencil() const noexcept override;
    // -- Metal-specific diagnostics (not in the contract). Metal exposes
    // no API version pair the way Vulkan's instance does: the device
    // name is the one diagnostic the backend can report.
    std::string deviceName() const noexcept;
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
};
}  // namespace render
}  // namespace grace
