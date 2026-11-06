#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "grace/render/render_backend.hpp"

namespace grace::render {

class NullRenderBackend;

/// Attachment view of a null render target (plain data, no graphics API).
class NullTextureView final : public TextureView
{
public:
    NullTextureView() = default;
    NullTextureView(std::uint32_t w, std::uint32_t h, PixelFormat format, bool depth)
        : m_width(w), m_height(h), m_format(format), m_depth(depth)
    {
    }

    std::uint32_t width() const noexcept override { return m_width; }
    std::uint32_t height() const noexcept override { return m_height; }
    PixelFormat format() const noexcept override { return m_format; }
    bool isDepth() const noexcept override { return m_depth; }

private:
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    PixelFormat m_format = PixelFormat::Rgba8;
    bool m_depth = false;
};

/// Render target of the null backend: records its desc and release state.
class NullRenderTarget final : public RenderTarget
{
public:
    explicit NullRenderTarget(const RenderTargetDesc& desc) : m_desc(desc)
    {
        for (std::uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
            colorViews.emplace_back(desc.width, desc.height, desc.colorFormats[i], false);
        if (desc.depthAttachment)
            depthView = NullTextureView(desc.width, desc.height, desc.depthFormat, true);
    }

    std::uint32_t width() const noexcept override { return m_desc.width; }
    std::uint32_t height() const noexcept override { return m_desc.height; }
    std::uint32_t colorAttachmentCount() const noexcept override
    {
        return m_desc.colorAttachmentCount;
    }
    bool hasColorAttachment() const noexcept override { return m_desc.colorAttachmentCount > 0; }
    bool hasDepthAttachment() const noexcept override { return m_desc.depthAttachment; }

    const RenderTargetDesc& desc() const noexcept { return m_desc; }
    bool released = false;

    std::vector<NullTextureView> colorViews;
    NullTextureView depthView;

private:
    RenderTargetDesc m_desc;
};

/// GPU mesh handle of the null backend: validates and records upload/draw
/// into the owning backend's event log and counts violations after shutdown.
class NullGpuMesh final : public GpuMesh
{
public:
    explicit NullGpuMesh(NullRenderBackend& owner) : m_owner(owner) {}

    void upload(const Mesh& mesh) override;
    void drawIndexed() const override;

private:
    NullRenderBackend& m_owner;
};

/// Shader program of the null backend: records use/setUniform calls into the
/// owning backend's event log. Uniform locations are stable fake values,
/// cached per name.
class NullShaderProgram final : public ShaderProgram
{
public:
    explicit NullShaderProgram(NullRenderBackend& owner) : m_owner(owner) {}

    void use() override;
    void setUniform(const char* name, float value) override;
    void setUniform(const char* name, int value) override;
    void setUniform(const char* name, const core::vec2& value) override;
    void setUniform(const char* name, const core::vec3& value) override;
    void setUniform(const char* name, const core::vec4& value) override;
    void setUniform(const char* name, const core::mat4& value) override;
    void setUniformSampler(const char* name, std::int32_t textureUnit) override;
    std::int32_t uniformLocation(const char* name) override;

private:
    NullRenderBackend& m_owner;
    UniformCache m_cache;
    std::int32_t m_next_location = 0;
};

/// Headless RenderBackend.
///
/// Records every call in order (for example "setClearColor(1,2,3,4)",
/// "clear(1,2,3,4)", "createGpuMesh", "upload", "draw", "readback",
/// "shutdown") and counts protocol violations: any mutating call after
/// shutdown, including a repeated shutdown. Violating calls are recorded but
/// do not change backend state.
///
/// Documented readback values: every pixel reports exactly the current clear
/// colour, depth 1.0 and stencil 0.
class NullRenderBackend final : public RenderBackend
{
public:
    /// Throws std::invalid_argument when width or height is zero.
    NullRenderBackend(std::uint32_t width, std::uint32_t height);

    std::string describe() const override;
    void setClearColor(ClearColor color) override;
    ClearColor clearColor() const override { return m_clear_color; }
    void setDepthTestEnabled(bool enabled) override;
    void clear() override;
    std::uint32_t width() const override { return m_width; }
    std::uint32_t height() const override { return m_height; }
    void shutdown() override;
    std::uint64_t violationCount() const noexcept override { return m_violations; }

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
    CameraParams camera() const override { return m_camera; }

    void bindTexture(const TextureView* view, std::uint32_t unit) override;
    void copyDepthAttachment(RenderTarget* source, RenderTarget* destination) override;

    void queueReadback(std::uint32_t x, std::uint32_t y) override;
    void readback() override;
    void swapBuffers() override;
    const std::vector<ReadbackSample>& lastReadback() const noexcept override
    {
        return m_last_readback;
    }
    bool hasCenterPixel() const noexcept override { return m_has_center; }
    ClearColor centerPixel() const noexcept override { return m_center_pixel; }
    float centerDepth() const noexcept override { return 1.0f; }
    std::int32_t centerStencil() const noexcept override { return 0; }

    /// Recorded call sequence, in order.
    const std::vector<std::string>& events() const noexcept { return m_events; }

private:
    friend class NullGpuMesh;
    friend class NullShaderProgram;

    void record(std::string event);
    bool ready() const noexcept { return !m_shutdown; }
    void countViolation() noexcept { ++m_violations; }
    NullRenderTarget* findTarget(RenderTarget* target) const;

    std::vector<std::unique_ptr<NullRenderTarget>> m_targets;
    RenderTarget* m_bound_target = nullptr;
    CameraParams m_camera;

    std::uint32_t m_width;
    std::uint32_t m_height;
    ClearColor m_clear_color{0, 0, 0, 255};
    bool m_shutdown = false;
    std::vector<std::string> m_events;
    std::uint64_t m_violations = 0;

    bool m_has_center = false;
    ClearColor m_center_pixel{0, 0, 0, 0};
    std::vector<ReadbackSample> m_last_readback;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> m_readback_queue;
};

}  // namespace grace::render
