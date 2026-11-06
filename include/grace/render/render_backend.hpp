#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "grace/render/draw_list.hpp"
#include "grace/render/mesh.hpp"
#include "grace/render/render_target.hpp"
#include "grace/render/shader.hpp"
#include "grace/render/shadow.hpp"

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

/// One pixel sample produced by readback().
struct ReadbackSample
{
    std::uint32_t x;
    std::uint32_t y;
    ClearColor color;
    float depth;
    std::int32_t stencil;
};

/// Platform-neutral rendering contract.
///
/// This header must stay free of any platform, windowing or graphics-API
/// types: no OpenGL, SDL, Vulkan, Metal, Direct3D or window handles may
/// appear here. Concrete backends (null, OpenGL) implement this interface in
/// their own modules. Mesh, shader and draw capabilities use the API-neutral
/// types (Mesh, GpuMesh, ShaderProgram) or opaque handles, never raw
/// graphics-API identifiers.
///
/// Protocol: mutating calls made before a backend is initialised (backends
/// with an explicit initialise step) or after shutdown() are protocol
/// violations and are counted through violationCount(); a repeated
/// shutdown() is a violation too but must stay safe.
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

    /// Releases backend resources. Exactly one successful call per backend
    /// lifetime; calls after shutdown are protocol violations.
    virtual void shutdown() = 0;

    /// Number of protocol violations observed so far.
    virtual std::uint64_t violationCount() const noexcept = 0;

    /// Compiles and links a shader program from source. Backends with a real
    /// compiler throw std::runtime_error carrying the compiler/linker info
    /// log on failure; the null backend validates the sources and records the
    /// call. Throws std::logic_error when the backend is not initialised.
    virtual std::unique_ptr<ShaderProgram> createShaderProgram(
        const std::string& vertexSource, const std::string& fragmentSource) = 0;

    /// Creates a GPU mesh handle. Upload is explicit via GpuMesh::upload;
    /// re-uploading replaces all previously uploaded data. Throws
    /// std::logic_error when the backend is not initialised.
    virtual std::unique_ptr<GpuMesh> createGpuMesh() = 0;

    /// Activates the program, sets a full-frame viewport and draws the mesh
    /// with an indexed draw. Calls before initialisation or after shutdown
    /// are protocol violations.
    virtual void draw(GpuMesh& mesh, ShaderProgram& program) = 0;

    /// Submits a lit draw (mesh + transform + surface + sun + dynamic lights,
    /// at most kMaxDynamicLightsPerObject of them) for drawing with the
    /// backend's forward lighting pass. The submission is validated first;
    /// invalid submissions throw std::invalid_argument. Calls before
    /// initialisation or after shutdown are protocol violations.
    virtual void submit(const DrawSubmission& submission) = 0;

    // -- Render targets ------------------------------------------------------
    //
    // Semantics (documented):
    // - createRenderTarget validates the desc and throws std::invalid_argument
    //   on an invalid one (zero size, or neither a colour nor a depth
    //   attachment: an "incomplete" target). The backend owns the handle.
    // - bindRenderTarget(nullptr) binds the default framebuffer. Binding a
    //   target while another is already bound simply replaces the binding
    //   (not a violation). clear()/draw()/submit()/readback() operate on the
    //   currently bound target.
    // - releaseRenderTarget invalidates the handle. Any later use of it
    //   (bind, texture view, shadow pass) is a protocol violation counted by
    //   violationCount(); the handle memory stays valid until shutdown so the
    //   misuse is detected safely. Binding a pointer that was never created
    //   is the same violation.
    // - renderTargetTexture returns the colour attachment view when present,
    //   otherwise the depth attachment view (depth-only shadow targets). It
    //   returns nullptr and counts a violation when the target is unknown,
    //   released, currently bound for rendering (feedback loop), or has no
    //   attachment to return.

    /// Creates a render target. Throws std::invalid_argument on an invalid
    /// or incomplete desc.
    virtual RenderTarget* createRenderTarget(const RenderTargetDesc& desc) = 0;

    /// Binds a target for rendering; nullptr binds the default framebuffer.
    virtual void bindRenderTarget(RenderTarget* target) = 0;

    /// Releases a target. Use after release is a protocol violation.
    virtual void releaseRenderTarget(RenderTarget* target) = 0;

    /// Attachment texture of a target, for sampling. nullptr + violation on
    /// unknown/released/feedback-bound targets.
    virtual TextureView* renderTargetTexture(RenderTarget* target) = 0;

    /// Depth-only shadow pass (milestone 4b): renders the submitted draw list
    /// from the sun's point of view into the target's depth attachment, using
    /// a single orthographic cascade covering `bounds`. Afterwards,
    /// subsequent submit() calls apply the stored shadow map to the sun's
    /// contribution only; dynamic lights stay unshadowed in this milestone.
    /// Releasing the target disables the shadow lookup. Validates sun, bounds
    /// and every submission (throws std::invalid_argument); a target that is
    /// unknown, released, currently bound, or depth-less is a violation.
    virtual void renderShadowMap(RenderTarget* target, const DirectionalLight& sun,
                                 const SceneBounds& bounds,
                                 const std::vector<DrawSubmission>& submissions) = 0;

    /// Queues a pixel to be captured by the next readback(). For backends with
    /// a buffer swap, readback captures before the swap (the back buffer is
    /// undefined afterwards).
    virtual void queueReadback(std::uint32_t x, std::uint32_t y) = 0;

    /// Captures the centre pixel and all queued pixels from the current read
    /// buffer. Documented value semantics per backend: the null backend
    /// reports exactly the current clear colour for every pixel (depth 1.0,
    /// stencil 0); the OpenGL backend reports the actual framebuffer contents
    /// (within rasterisation tolerance of one unit per channel).
    virtual void readback() = 0;

    /// Samples captured by the last readback(), in queue order.
    virtual const std::vector<ReadbackSample>& lastReadback() const noexcept = 0;

    /// Whether a centre-pixel sample has been captured.
    virtual bool hasCenterPixel() const noexcept = 0;
    virtual ClearColor centerPixel() const noexcept = 0;
    virtual float centerDepth() const noexcept = 0;
    virtual std::int32_t centerStencil() const noexcept = 0;
};

}  // namespace grace::render
