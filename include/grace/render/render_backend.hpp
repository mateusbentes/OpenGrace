#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "grace/render/camera.hpp"
#include "grace/render/draw_list.hpp"
#include "grace/render/mesh.hpp"
#include "grace/render/render_target.hpp"
#include "grace/render/shader.hpp"

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

/// Buffer selection for clear(), as a platform-neutral bitmask. Every
/// backend's config carries its own default; the mask values are shared.
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

    /// Enables or disables depth testing for subsequent passes (device
    /// state, default enabled). When disabled, no depth test is performed
    /// and no depth writes occur, so fullscreen passes never overwrite a
    /// target's scene depth (decision 20, milestone 6d). Re-enable before
    /// geometry passes. Calls after shutdown are protocol violations.
    virtual void setDepthTestEnabled(bool enabled) = 0;

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

    /// Compiles and links a shader program for the given API-neutral program
    /// identity; the backend owns the source (decision 14). Throws
    /// std::runtime_error carrying the compiler/linker info log on failure.
    /// Throws std::logic_error when the backend is not initialised.
    virtual std::unique_ptr<ShaderProgram> createShaderProgram(ShaderProgramId id) = 0;

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

    // -- Render passes and targets -------------------------------------------
    //
    // The contract is technique-agnostic (open decision 7, resolved 2026-11-06:
    // generic render-pass composition instead of technique-named methods, so a
    // deferred path can reuse the same primitives). A pass is:
    // beginRenderPass(target) ... clear()/draw()/submit() ... endRenderPass().
    //
    // Semantics (documented):
    // - createRenderTarget validates the desc and throws std::invalid_argument
    //   on an invalid one (zero size, or neither a colour nor a depth
    //   attachment: an "incomplete" target). The backend owns the handle.
    // - beginRenderPass(nullptr) targets the default framebuffer. Beginning a
    //   pass while another target is already bound simply replaces the
    //   binding (not a violation). clear()/draw()/submit()/readback() operate
    //   on the current pass's target. endRenderPass() returns to the default
    //   framebuffer.
    // - releaseRenderTarget invalidates the handle. Any later use of it
    //   (pass begin, texture view) is a protocol violation counted by
    //   violationCount(); the handle memory stays valid until shutdown so the
    //   misuse is detected safely. A pointer that was never created is the
    //   same violation.
    // - renderTargetTexture returns the colour attachment view when present,
    //   otherwise the depth attachment view (depth-only shadow targets). It
    //   returns nullptr and counts a violation when the target is unknown,
    //   released, inside a render pass (feedback loop), or has no attachment
    //   to return.

    /// Creates a render target. Throws std::invalid_argument on an invalid
    /// or incomplete desc.
    virtual RenderTarget* createRenderTarget(const RenderTargetDesc& desc) = 0;

    /// Begins a render pass on a target; nullptr targets the default
    /// framebuffer. Replaces any current binding.
    virtual void beginRenderPass(RenderTarget* target) = 0;

    /// Ends the current render pass (back to the default framebuffer).
    virtual void endRenderPass() = 0;

    /// Releases a target. Use after release is a protocol violation.
    virtual void releaseRenderTarget(RenderTarget* target) = 0;

    /// Attachment texture of a target, for sampling. nullptr + violation on
    /// unknown/released/feedback-bound targets. Returns colour attachment 0
    /// when present, otherwise the depth attachment (backward compatible).
    virtual TextureView* renderTargetTexture(RenderTarget* target) = 0;

    /// Attachment texture by index: index < colorAttachmentCount selects that
    /// colour attachment; index == kDepthAttachment selects the depth
    /// attachment. nullptr + violation on unknown/released/feedback-bound
    /// targets or an out-of-range index.
    virtual TextureView* renderTargetTexture(RenderTarget* target,
                                             std::uint32_t attachment) = 0;

    /// Binds an attachment texture for sampling on a texture unit (a device
    /// operation, like draw). Calls after shutdown are protocol violations.
    virtual void bindTexture(const TextureView* view, std::uint32_t unit) = 0;

    // -- 3D textures (milestone 7, first increment) ---------------------------
    //
    // A volume is a standalone resource, not a render target: it owns no
    // framebuffer and no pass renders into it yet. What it must do is hold
    // values that survive a write and a read, which is what the round trip
    // through fillTexture3D() and readTexture3D() proves on its own, before
    // any light propagation exists.

    /// Creates a 3D texture and returns its view for sampling. The view's
    /// depth() reports the volume's own layer count. Throws
    /// std::invalid_argument on an invalid desc (a zero dimension, or the
    /// depth/stencil format, which cannot describe a volume).
    virtual TextureView* createTexture3D(const Texture3DDesc& desc) = 0;

    /// Binds a 3D texture for sampling on a texture unit. A view that is not a
    /// volume is a violation, as is a call after shutdown.
    virtual void bindTexture3D(const TextureView* view, std::uint32_t unit) = 0;

    /// Writes one 2D slice of a volume: layer z of view, from width * height *
    /// bytesPerPixel(format) bytes in row-major order, rows top to bottom, no
    /// row padding. Throws std::invalid_argument when the layer is out of
    /// range or the byte count does not match the volume. Calls after shutdown
    /// are protocol violations.
    virtual void fillTexture3D(TextureView* view, std::uint32_t layer, const void* data,
                               std::size_t sizeBytes) = 0;

    /// Reads one 2D slice of a volume back into data, in the same order and
    /// with the same size rule as fillTexture3D. Returns false and counts a
    /// violation when the layer is out of range or the byte count does not
    /// match. This is the round trip the volume capability is proved with.
    virtual bool readTexture3D(const TextureView* view, std::uint32_t layer, void* data,
                               std::size_t sizeBytes) = 0;
    /// Releases a volume created by createTexture3D. The view is invalid after
    /// this call and any later use is a violation, the same ownership rule
    /// releaseRenderTarget states for targets. Added with the third increment
    /// of milestone 7: the first increment could create a volume but had no way
    /// to release one before shutdown, so a renderer that rebuilt its volume on
    /// a configuration change could only leak it.
    virtual void releaseTexture3D(TextureView* view) = 0;

    // -- Rendering into a volume (milestone 7, second increment) --------------
    //
    // A pass writes one layer of a volume. The result is a RenderTarget, so
    // beginRenderPass, endRenderPass and releaseRenderTarget are the existing
    // ones and no second pass protocol exists: what changes is only how the
    // backend builds the framebuffer, attaching layer z of the volume instead
    // of allocating a 2D texture.
    //
    // A layer target borrows the volume; it does not own it. Releasing the
    // target must leave the volume and its contents intact, which is the
    // ownership rule that makes this safe to iterate with.

    /// Returns a render target that writes one layer of an existing volume.
    /// The target's width and height are the volume's, it has exactly one
    /// colour attachment and no depth attachment, and rendering into it changes
    /// that layer and no other. Throws std::invalid_argument when the view is
    /// not a volume created by this backend or the layer is out of range.
    virtual RenderTarget* createVolumeLayerTarget(TextureView* volume,
                                                  std::uint32_t layer) = 0;

    /// Copies the depth attachment of source onto destination (a generic
    /// pass primitive, milestone 6d decision 20: moves scene depth between
    /// targets, e.g. G-buffer -> HDR in the deferred path, so later passes
    /// read scene depth from one target in both paths). Both targets must
    /// be known, unreleased, have a depth attachment and match in size;
    /// source == destination or either target currently bound in a pass is
    /// a violation (counted, no copy performed). Calls after shutdown are
    /// protocol violations.
    virtual void copyDepthAttachment(RenderTarget* source, RenderTarget* destination) = 0;

    // -- Camera and path selection --------------------------------------------

    /// Sets the camera used by submit()/draw() (view-projection). Throws
    /// std::invalid_argument on invalid parameters.
    virtual void setCamera(const CameraParams& camera) = 0;
    virtual CameraParams camera() const = 0;


    /// Presents the completed frame: captures any queued readback and swaps
    /// the back buffer. Called once at the end of a frame, after all drawing.
    /// A device capability, not a technique: it does not depend on the render
    /// path. clear() no longer presents; before milestone 6a it swapped as a
    /// side effect, which meant a mid-frame clear presented half a frame.
    virtual void swapBuffers() = 0;

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
