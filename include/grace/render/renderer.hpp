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

/// Cap on volumetric lights (volumeDensity > 0) in one frame's volume pass
/// (milestone 6f). The cap is the volume shader's uniform array size,
/// documented and enforced: a frame that would exceed it is rejected with
/// std::invalid_argument in submit(), never truncated. Per-light cost is
/// per-pixel: 16 ray samples x (attenuation + shadow lookup) each.
inline constexpr std::uint32_t kMaxVolumetricLights = 4;

/// Post-processing chain configuration (milestone 6a). Off by default; with it
/// off, every pixel value is bit-identical to before the chain existed (H4).
/// The chain runs after lighting and before frame completion, owned by the
/// renderer, and is path-independent (forward and deferred feed it).
/// SSAO configuration (milestone 6c), part of the post chain. The occlusion
/// factor multiplies the ambient term only (decision 18). Temporal filtering
/// (SSAO-STF) is NOT implemented — recorded deferred to its own increment.
struct SSAOConfig
{
    bool enabled = true;          ///< on when the post chain is on
    float radius = 0.5f;          ///< world units
    std::uint32_t samples = 8;    ///< hemisphere samples (max 8 in the shader)
    float bias = 0.02f;           ///< world units along the normal (self-occlusion guard)

    void validate() const;
};

/// Morphological anti-aliasing configuration (milestone 6d; runs on the
/// display-encoded image since 6e — the chain ends in a sampleable offscreen
/// 8-bit target, decision 22 update). A simple morphological pass — NOT SMAA
/// (no pattern search, no reprojection). Edges: Rec709 luminance contrast of
/// the display codes, or NDC depth contrast. Default ON with the chain
/// (decision 23): view-independent, part of the documented chain.
struct MlaaConfig
{
    bool enabled = true;               ///< on with the chain (decision 23, 6e)
    float luminanceThreshold = 0.08f;  ///< display-code luminance delta marking an edge
    float depthThreshold = 0.02f;      ///< NDC depth delta marking an edge

    void validate() const;
};

/// Depth-of-field configuration (milestone 6d). Circle of confusion:
/// coc = clamp(aperture * |d - focusDistance| / d, 0, maxCocPixels), d the
/// view distance from the scene depth; focusDistance/aperture in world
/// units, maxCocPixels in pixels. Blur: 16-tap bokeh disc, single gather
/// pass in linear HDR. Off by default: the focus distance is scene content,
/// not a pipeline default (decision 23, 6e) — a default focus would blur
/// every scene unless it happened to match.
struct DofConfig
{
    bool enabled = false;          ///< off by default (decision 23: needs scene focus)
    float focusDistance = 1.0f;    ///< world units: the sharp plane
    float aperture = 0.5f;         ///< world units: CoC scale
    float maxCocPixels = 12.0f;    ///< CoC clamp, pixels

    void validate() const;
};

struct PostConfig
{
    /// Post-processing ON by default (milestone 6e, decision 23 — the flip
    /// decision 15 committed to once the techniques landed). The legacy
    /// linear->8-bit path survives as an explicit opt-out (compatibility
    /// path, e.g. CLI --no-post), not as the default.
    bool enabled = true;
    float exposure = 1.0f;            ///< tonemap exposure, linear multiplier
    float bloomThreshold = 1.0f;      ///< linear HDR threshold for the bright pass
    std::uint32_t bloomLevels = 4;    ///< downsample chain depth (half-res each)
    float bloomStrength = 1.0f;       ///< bloom compose weight
    SSAOConfig ssao;                  ///< SSAO (6c), darkens the ambient term only
    MlaaConfig mlaa;                  ///< morphological AA (6d), linear HDR, off by default
    DofConfig dof;                    ///< depth of field with bokeh (6d), off by default

    void validate() const;
};

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
    /// Scene-global ambient (indirect) term, linear, same units as light
    /// intensity (milestone 6b, decision 17 — a documented deviation from the
    /// documented light types, which list no ambient). Default 0.2, chosen on
    /// measured evidence in decision 21 (6d): at 0.05 one SSAO occlusion step
    /// (1/8) moved a lit pixel by 0 eight-bit levels; at 0.2 the full occlusion
    /// range moves a lit pixel by 14 levels (220->206) and an ambient-only
    /// corner by 50 (149->99), while occluded surfaces still read 51/255
    /// legacy — shadows stay readable.
    float ambient = 0.2f;
    PostConfig post;                        ///< post-processing chain (on by default since 6e)

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

    /// The HDR lighting target while the post chain is on, for inspection
    /// only (readback of its attachments and scene depth through the backend).
    /// nullptr when the post chain is off.
    RenderTarget* hdrTarget() noexcept { return m_hdr; }

    /// The offscreen display target the last present pass copied from (the
    /// chain's final 8-bit image), for inspection only. nullptr when the post
    /// chain is off.
    RenderTarget* displayTarget() noexcept { return m_displaySrc; }


    std::uint64_t frameCount() const noexcept { return m_frame_count; }

private:
    void renderForwardFrame();
    void renderDeferredFrame();
    void renderShadowMapsIfNeeded();
    void runPostChain();
    std::uint32_t volumetricLightCount() const;
    RenderTarget* ensureHdrTarget();
    void ensureFullscreenQuad();
    void releasePostResources();
    RenderTarget* lightingTarget();
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
    // Post chain resources (6a): HDR target + bloom chain, created on the
    // first post-enabled frame, released on disable or size change (documented).
    RenderTarget* m_hdr = nullptr;
    std::vector<RenderTarget*> m_bloom;
    std::unique_ptr<ShaderProgram> m_postBright;
    std::unique_ptr<ShaderProgram> m_postBlur;
    std::unique_ptr<ShaderProgram> m_postTonemap;
    std::unique_ptr<ShaderProgram> m_postSSAO;
    RenderTarget* m_ssao = nullptr;  ///< R8 occlusion buffer, full framebuffer size
    std::unique_ptr<ShaderProgram> m_postMLAA;  ///< 6d morphological AA pass (display domain since 6e)
    std::unique_ptr<ShaderProgram> m_postDOF;   ///< 6d depth-of-field pass
    std::unique_ptr<ShaderProgram> m_postBlit;  ///< 6e present pass (display target -> default FB)
    RenderTarget* m_postA = nullptr;  ///< 6d ping-pong target: 2x Rgba16F, full framebuffer size
    RenderTarget* m_display = nullptr;   ///< 6e offscreen 8-bit display target (Rgba8, sampleable)
    RenderTarget* m_displayB = nullptr;  ///< 6e MLAA ping-pong on the display image (Rgba8)
    RenderTarget* m_displaySrc = nullptr;///< 6e the target the last present pass copied from
    std::unique_ptr<ShaderProgram> m_postVolumetric;  ///< 6f volumetric in-scattering pass
    RenderTarget* m_vol = nullptr;  ///< 6f linear in-scattering buffer: Rgba16F, full framebuffer size
    std::unique_ptr<ShaderProgram> m_gbuffer_program;
    std::unique_ptr<ShaderProgram> m_deferred_program;
    std::unique_ptr<GpuMesh> m_fullscreen_quad;
    std::vector<DynamicLight> m_frame_lights;
    std::optional<DirectionalLight> m_frame_sun;
};

}  // namespace grace::render
