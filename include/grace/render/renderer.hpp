#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "grace/render/camera.hpp"
#include "grace/render/draw_list.hpp"
#include "grace/render/light.hpp"
#include "grace/render/render_backend.hpp"
#include "grace/render/render_target.hpp"
#include "grace/render/shadow.hpp"

namespace grace::render {

/// Rendering path selection. Lives in the renderer, not the backend (decision
/// 12/13 layering): the path is frame composition, not a device capability.
enum class RenderPath : std::uint8_t
{
    Forward = 0,   ///< default
    Deferred = 1
};

/// Cap on dynamic lights in one deferred lighting pass. The original's deferred
/// path was unlimited; here the cap is the shader's uniform array size,
/// documented and enforced: a frame that would exceed it is rejected with
/// std::invalid_argument, never truncated.
inline constexpr std::uint32_t kMaxDeferredLights = 16;

/// Renderer configuration. Configuration reaches the renderer, never the
/// backend (the backend only receives device-level state such as the camera
/// and the clear colour, forwarded at frame begin).
struct RendererConfig
{
    RenderPath path = RenderPath::Forward;  ///< default forward (decision 1)
    ClearColor clear_color{0, 0, 0, 255};
    CameraParams camera;                    ///< diagnostic default
    CascadeConfig cascades;                 ///< sun shadow cascades (forward)
    PointShadowConfig pointShadows;         ///< paraboloid point shadows (forward)
    bool shadows = true;                    ///< run the shadow passes in forward
    std::uint32_t deferredLightCap = kMaxDeferredLights;

    /// Throws std::invalid_argument on invalid values (out-of-range path,
    /// invalid camera/cascade/point-shadow config, zero deferredLightCap).
    void validate() const;
};

/// The renderer layer (milestone 5b): owns the frame and composes it from the
/// backend's pass primitives — the backend is the device, the renderer is the
/// composition (decision 7's shape, applied to frames; decision 12 closed by
/// this layer).
///
/// Frame boundary (documented): beginFrame() begins a frame (binds the default
/// framebuffer and clears it; in deferred mode it clears the G-buffer),
/// submit() buffers a draw submission, endFrame() assembles the passes (shadow
/// passes, then geometry/lighting) and completes the frame. The frame's result
/// is readable through the backend's readback after endFrame() returns.
///
/// Not a god object: resource management, scene storage, culling and asset
/// loading are NOT frame composition and do not belong here.
class Renderer
{
public:
    explicit Renderer(RenderBackend& backend);

    /// Applies configuration. Throws std::invalid_argument on invalid values.
    void setConfig(const RendererConfig& config);
    const RendererConfig& config() const noexcept { return m_config; }

    /// Clear colour — device-level state, forwarded to the backend.
    void setClearColor(ClearColor color);

    /// Begins a frame. Throws std::logic_error when a frame is already open.
    void beginFrame();

    /// Buffers a draw submission for the current frame. Validates it (throws
    /// std::invalid_argument on invalid submissions).
    void submit(const DrawSubmission& submission);

    /// Completes the frame: assembles and runs the passes. After this returns,
    /// the frame's result is readable through the backend.
    void endFrame();

    /// Device access for lifecycle and inspection (shutdown, readback).
    RenderBackend& backend() noexcept { return m_backend; }

    /// The deferred G-buffer target, for inspection only (readback of its
    /// attachments through the backend). nullptr in the forward path.
    RenderTarget* gbufferTarget() noexcept { return m_gbuffer; }

    std::uint64_t frameCount() const noexcept { return m_frame_count; }

private:
    void renderForwardFrame();
    void renderDeferredFrame();
    void ensureDeferredResources();
    void releaseShadowMaps();

    RenderBackend& m_backend;
    RendererConfig m_config;
    bool m_frame_open = false;
    std::uint64_t m_frame_count = 0;
    std::vector<DrawSubmission> m_pending;

    // Shadow maps of the current frame (owned by the renderer).
    ShadowMap m_shadow_map;
    PointShadowMaps m_point_shadow_maps;
    bool m_shadow_maps_valid = false;

    // Deferred resources (created through the contract, cached per size).
    RenderTarget* m_gbuffer = nullptr;
    std::unique_ptr<ShaderProgram> m_gbuffer_program;
    std::unique_ptr<ShaderProgram> m_deferred_program;
    std::unique_ptr<GpuMesh> m_fullscreen_quad;
    std::vector<DynamicLight> m_frame_lights;
    std::optional<DirectionalLight> m_frame_sun;
};

}  // namespace grace::render
