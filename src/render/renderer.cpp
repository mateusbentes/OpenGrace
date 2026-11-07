#include "grace/render/renderer.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "grace/core/math/camera.hpp"

namespace grace::render {

namespace {

/// Scattering density of a dynamic light (0 = not volumetric).
float volumetricDensity(const DynamicLight& light)
{
    if (const auto* point = std::get_if<PointLight>(&light))
        return point->volumeDensity;
    if (const auto* spot = std::get_if<SpotLight>(&light))
        return spot->volumeDensity;
    return 0.0f;
}

}  // namespace

void SSAOConfig::validate() const
{
    if (!(radius > 0.0f))
        throw std::invalid_argument("ssao config: radius must be > 0 (world units)");
    if (samples == 0 || samples > 8)
        throw std::invalid_argument("ssao config: samples must be in [1, 8]");
    if (!(bias >= 0.0f))
        throw std::invalid_argument("ssao config: bias must be >= 0");
}

void MlaaConfig::validate() const
{
    if (!(luminanceThreshold > 0.0f))
        throw std::invalid_argument("mlaa config: luminanceThreshold must be > 0 (linear)");
    if (!(depthThreshold > 0.0f))
        throw std::invalid_argument("mlaa config: depthThreshold must be > 0 (NDC)");
}

void DofConfig::validate() const
{
    if (!(focusDistance > 0.0f))
        throw std::invalid_argument("dof config: focusDistance must be > 0 (world units)");
    if (!(aperture >= 0.0f))
        throw std::invalid_argument("dof config: aperture must be >= 0 (world units)");
    if (!(maxCocPixels > 0.0f))
        throw std::invalid_argument("dof config: maxCocPixels must be > 0");
}

void PostConfig::validate() const
{
    ssao.validate();
    mlaa.validate();
    dof.validate();
    if (!(exposure > 0.0f))
        throw std::invalid_argument("post config: exposure must be > 0");
    if (!(bloomThreshold >= 0.0f))
        throw std::invalid_argument("post config: bloomThreshold must be >= 0");
    if (bloomLevels == 0 || bloomLevels > 8)
        throw std::invalid_argument("post config: bloomLevels must be in [1, 8]");
}

void RendererConfig::validate() const
{
    post.validate();
    if (path != RenderPath::Forward && path != RenderPath::Deferred)
        throw std::invalid_argument("renderer config: out-of-range render path");
    camera.validate();
    validateCascadeConfig(cascades);
    validatePointShadowConfig(pointShadows);
    if (deferredLightCap == 0)
        throw std::invalid_argument("renderer config: deferredLightCap must be > 0");
    if (!(ambient >= 0.0f))
        throw std::invalid_argument("renderer config: ambient must be >= 0 (linear)");
}

Renderer::Renderer(RenderBackend& backend) : m_backend(backend) {}

void Renderer::setConfig(const RendererConfig& config)
{
    config.validate();  // throws std::invalid_argument
    const bool wasPost = m_config.post.enabled;
    m_config = config;
    releaseShadowMaps();
    if (wasPost && !m_config.post.enabled)
        releasePostResources();  // documented: released when the chain is disabled
}

void Renderer::setClearColor(ClearColor color)
{
    m_config.clear_color = color;
    m_backend.setClearColor(color);  // device-level state, forwarded
}

void Renderer::beginFrame()
{
    if (m_frame_open)
        throw std::logic_error("Renderer::beginFrame: frame already open");
    m_frame_open = true;
    m_pending.clear();
    m_frame_lights.clear();
    m_frame_sun.reset();
    m_shadow_maps_valid = false;

    m_backend.setCamera(m_config.camera);
    if (m_config.path != RenderPath::Deferred)
    {
        // Forward: lighting writes into the HDR target when post is on,
        // else the default framebuffer (unchanged legacy values when off).
        m_backend.beginRenderPass(lightingTarget());
        m_backend.clear();
    }
    // Deferred: the G-buffer pass opens in endFrame, after the shadow passes.
}

void Renderer::submit(const DrawSubmission& submission)
{
    if (!m_frame_open)
        throw std::logic_error("Renderer::submit: no frame open");
    if (m_config.path == RenderPath::Deferred)
    {
        // Deferred: the per-object limit does not apply; the deferred cap does.
        submission.validate(m_config.deferredLightCap);
        if (m_frame_lights.size() + submission.lights.size() > m_config.deferredLightCap)
            throw std::invalid_argument(
                "Renderer::submit (deferred): frame would exceed deferredLightCap; rejected, "
                "not truncated");
    }
    else
    {
        submission.validate();  // forward: per-object limit of 4
    }
    m_pending.push_back(submission);
    for (const DynamicLight& light : submission.lights)
    {
        m_frame_lights.push_back(light);
        if (volumetricDensity(light) > 0.0f &&
            volumetricLightCount() >= kMaxVolumetricLights)
            throw std::invalid_argument(
                "submit: frame would exceed kMaxVolumetricLights volumetric lights; "
                "rejected, not truncated");
    }
    if (submission.sun.has_value() && !m_frame_sun.has_value())
        m_frame_sun = submission.sun;
}

void Renderer::endFrame()
{
    if (!m_frame_open)
        throw std::logic_error("Renderer::endFrame: no frame open");
    if (m_config.path == RenderPath::Deferred)
        renderDeferredFrame();
    else
        renderForwardFrame();
    if (m_config.post.enabled)
        runPostChain();  // after lighting, before frame completion (H4)
    m_backend.endRenderPass();
    // Presentation is explicit and happens once, at the end of the frame.
    // Before milestone 6a it was a side effect of clear(), so a mid-frame
    // clear presented half a frame and the readback captured before the
    // tonemap could run.
    m_backend.swapBuffers();
    m_frame_open = false;
    ++m_frame_count;
    m_pending.clear();
}

void Renderer::releaseShadowMaps()
{
    if (m_shadow_maps_valid)
    {
        for (RenderTarget* target : m_shadow_map.targets)
            m_backend.releaseRenderTarget(target);
        for (const PointShadowMapEntry& entry : m_point_shadow_maps.entries)
        {
            m_backend.releaseRenderTarget(entry.front);
            m_backend.releaseRenderTarget(entry.back);
        }
    }
    m_shadow_map = ShadowMap{};
    m_point_shadow_maps = PointShadowMaps{};
    m_shadow_maps_valid = false;
}

RenderTarget* Renderer::lightingTarget()
{
    return m_config.post.enabled ? ensureHdrTarget() : nullptr;
}

RenderTarget* Renderer::ensureHdrTarget()
{
    if (m_hdr != nullptr && (m_hdr->width() != m_backend.width() ||
                             m_hdr->height() != m_backend.height()))
        releasePostResources();
    if (m_hdr == nullptr)
    {
        // HDR target: float colour + depth attachment (the depth texture is
        // sampleable for the 6b/6c techniques that read depth as a texture).
        RenderTargetDesc desc;
        desc.width = m_backend.width();
        desc.height = m_backend.height();
        // Two colour attachments (decision 11, cap 4): 0 = direct+ambient as
        // before, 1 = ambient alone so SSAO can scale it (6c, decision 18).
        desc.colorAttachmentCount = 2;
        desc.colorFormats[0] = PixelFormat::Rgba16F;  // linear HDR
        desc.colorFormats[1] = PixelFormat::Rgba16F;  // ambient only, linear
        desc.depthAttachment = true;
        desc.depthFormat = PixelFormat::Depth24Stencil8;  // sampleable depth for SSAO
        m_hdr = m_backend.createRenderTarget(desc);

        // Occlusion buffer: R8, full framebuffer size, no depth (~1 B/pixel).
        RenderTargetDesc sdesc;
        sdesc.width = m_backend.width();
        sdesc.height = m_backend.height();
        sdesc.colorAttachmentCount = 1;
        sdesc.colorFormats[0] = PixelFormat::R8;
        sdesc.depthAttachment = false;
        m_ssao = m_backend.createRenderTarget(sdesc);

        // 6d ping-pong target: 2x Rgba16F (colour + ambient), full framebuffer
        // size (~16.6 MB colour at 1280x720). The MLAA/DOF passes read one
        // HDR image and write the other; depth always stays in m_hdr.
        RenderTargetDesc pdesc;
        pdesc.width = m_backend.width();
        pdesc.height = m_backend.height();
        pdesc.colorAttachmentCount = 2;
        pdesc.colorFormats[0] = PixelFormat::Rgba16F;
        pdesc.colorFormats[1] = PixelFormat::Rgba16F;
        pdesc.depthAttachment = false;
        m_postA = m_backend.createRenderTarget(pdesc);

        // 6e (decision 22 update): the chain ends in an offscreen 8-bit display
        // target, which IS sampleable through the contract — so MLAA can run
        // on the display-encoded image, and the present pass (PostBlit) copies
        // it onto the default framebuffer. Rgba8, full framebuffer size,
        // ~4 bytes/pixel (~3.7 MB at 1280x720) each.
        RenderTargetDesc ddesc;
        ddesc.width = m_backend.width();
        ddesc.height = m_backend.height();
        ddesc.colorAttachmentCount = 1;
        ddesc.colorFormats[0] = PixelFormat::Rgba8;
        ddesc.depthAttachment = false;
        m_display = m_backend.createRenderTarget(ddesc);
        m_displayB = m_backend.createRenderTarget(ddesc);

        // 6f: linear in-scattering buffer for volumetric lights (Rgba16F,
        // ~8 bytes/pixel, ~7.4 MB at 1280x720), no depth.
        RenderTargetDesc vdesc;
        vdesc.width = m_backend.width();
        vdesc.height = m_backend.height();
        vdesc.colorAttachmentCount = 1;
        vdesc.colorFormats[0] = PixelFormat::Rgba16F;
        vdesc.depthAttachment = false;
        m_vol = m_backend.createRenderTarget(vdesc);

        // Bloom downsample chain: half resolution each level, same aspect as
        // the framebuffer (the chain's uv math assumes matching aspect), float,
        // no depth.
        m_bloom.clear();
        std::uint32_t bw = m_backend.width() / 2u;
        std::uint32_t bh = m_backend.height() / 2u;
        for (std::uint32_t level = 0; level < m_config.post.bloomLevels && bw >= 1u && bh >= 1u;
             ++level)
        {
            RenderTargetDesc bdesc;
            bdesc.width = bw;
            bdesc.height = bh;
            bdesc.colorAttachmentCount = 1;
            bdesc.colorFormats[0] = PixelFormat::Rgba16F;
            bdesc.depthAttachment = false;
            m_bloom.push_back(m_backend.createRenderTarget(bdesc));
            bw /= 2u;
            bh /= 2u;
        }

        m_postBright = m_backend.createShaderProgram(ShaderProgramId::PostBright);
        m_postBlur = m_backend.createShaderProgram(ShaderProgramId::PostBlur);
        m_postTonemap = m_backend.createShaderProgram(ShaderProgramId::PostTonemap);
        m_postSSAO = m_backend.createShaderProgram(ShaderProgramId::PostSSAO);
        m_postMLAA = m_backend.createShaderProgram(ShaderProgramId::PostMLAA);
        m_postDOF = m_backend.createShaderProgram(ShaderProgramId::PostDOF);
        m_postBlit = m_backend.createShaderProgram(ShaderProgramId::PostBlit);
        m_postVolumetric = m_backend.createShaderProgram(ShaderProgramId::PostVolumetric);
        ensureFullscreenQuad();
    }
    return m_hdr;
}

void Renderer::ensureFullscreenQuad()
{
    if (!m_fullscreen_quad)
    {
        Mesh quad;
        VertexLayout layout;
        layout.stride = 2 * sizeof(float);
        layout.attributes.push_back(
            VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 2, 0, false});
        quad.setLayout(layout);
        quad.setVertexData(std::vector<float>{-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f});
        quad.setIndices(std::vector<std::uint32_t>{0, 1, 2});
        m_fullscreen_quad = m_backend.createGpuMesh();
        m_fullscreen_quad->upload(quad);
    }
}

void Renderer::releasePostResources()
{
    if (m_hdr != nullptr)
    {
        m_backend.releaseRenderTarget(m_hdr);
        m_hdr = nullptr;
    }
    for (RenderTarget* target : m_bloom)
        m_backend.releaseRenderTarget(target);
    m_bloom.clear();
    if (m_ssao != nullptr)
    {
        m_backend.releaseRenderTarget(m_ssao);
        m_ssao = nullptr;
    }
    if (m_postA != nullptr)
    {
        m_backend.releaseRenderTarget(m_postA);
        m_postA = nullptr;
    }
    for (RenderTarget* target : {m_display, m_displayB})
    {
        if (target != nullptr)
            m_backend.releaseRenderTarget(target);
    }
    m_display = nullptr;
    m_displayB = nullptr;
    m_displaySrc = nullptr;
    if (m_vol != nullptr)
    {
        m_backend.releaseRenderTarget(m_vol);
        m_vol = nullptr;
    }
    m_postBright.reset();
    m_postBlur.reset();
    m_postTonemap.reset();
    m_postSSAO.reset();
    m_postMLAA.reset();
    m_postDOF.reset();
    m_postBlit.reset();
    m_postVolumetric.reset();
}

void Renderer::runPostChain()
{
    // End the lighting pass first: the HDR target must not be bound while the
    // chain samples it (feedback loop).
    m_backend.endRenderPass();
    // Decision 19 (6d base repair): the HDR target's depth attachment is the
    // scene depth in BOTH paths. Forward wrote it with the geometry; deferred
    // keeps it in the G-buffer, so copy it over now that the lighting quad
    // (which runs with the depth test off and writes no depth) is done.
    if (m_config.path == RenderPath::Deferred && m_gbuffer != nullptr)
        m_backend.copyDepthAttachment(m_gbuffer, m_hdr);
    // Every pass in the chain is a fullscreen pass: run them with the depth
    // test off so none of them tests against or overwrites scene depth.
    m_backend.setDepthTestEnabled(false);
    // Chain order (decision 21 as updated by decision 22/23 in 6e), one line:
    //   lighting -> HDR(2 att + scene depth) -> SSAO(depth -> R8)
    //   -> DOF(linear HDR, bokeh) -> bloom
    //   -> tonemap: sRGB(aces(direct + amb*occ + bloom)) [DISPLAY BOUNDARY]
    //   -> offscreen 8-bit display target -> MLAA(display-encoded)
    //   -> present (PostBlit) -> default framebuffer -> swapBuffers.
    // `src` tracks the target currently holding the HDR image (colour 0 +
    // ambient 1); the scene depth always stays in m_hdr (decision 20).
    ensureHdrTarget();
    ensureFullscreenQuad();
    RenderTarget* src = m_hdr;

    // Volumetric light pass (6f): in-scattering along the view ray into m_vol,
    // in LINEAR HDR. Reads the HDR scene depth (decision 20) and the paraboloid
    // shadow maps of shadowed point-style volumes; runs in the shared chain, so
    // forward and deferred get identical volumes. Skipped entirely when no
    // light has volumeDensity > 0 (the default — no pixel changes).
    const std::uint32_t volCount = volumetricLightCount();
    if (volCount > 0 && m_vol != nullptr)
    {
        const core::mat4 view = core::lookAt(m_config.camera.eye, m_config.camera.target,
                                              m_config.camera.up);
        m_backend.beginRenderPass(m_vol);
        m_backend.clear();
        m_backend.bindTexture(m_backend.renderTargetTexture(m_hdr, kDepthAttachment), 0);
        // Paraboloid shadow maps (slots 0..1 -> texture units 1..4), bound the
        // same way the deferred lighting pass binds them.
        const std::size_t entryCount = m_point_shadow_maps.entries.size();
        for (std::size_t k = 0; k < entryCount && k < 2; ++k)
        {
            const PointShadowMapEntry& entry = m_point_shadow_maps.entries[k];
            m_backend.bindTexture(m_backend.renderTargetTexture(entry.front, kDepthAttachment),
                                  1u + 2u * static_cast<std::uint32_t>(k));
            m_backend.bindTexture(m_backend.renderTargetTexture(entry.back, kDepthAttachment),
                                  2u + 2u * static_cast<std::uint32_t>(k));
        }
        m_postVolumetric->use();
        m_postVolumetric->setUniform("depthTex", 0);
        m_postVolumetric->setUniform("pointShadowFront[0]", 1);
        m_postVolumetric->setUniform("pointShadowBack[0]", 2);
        m_postVolumetric->setUniform("pointShadowFront[1]", 3);
        m_postVolumetric->setUniform("pointShadowBack[1]", 4);
        m_postVolumetric->setUniform("invProj", core::perspectiveInverse(
            m_config.camera.fovYRad, m_config.camera.aspect, m_config.camera.near, m_config.camera.far));
        m_postVolumetric->setUniform("invView", core::inverseAffine(view));
        m_postVolumetric->setUniform("cameraPos", m_config.camera.eye);
        m_postVolumetric->setUniform("outSize", core::vec2{static_cast<float>(m_vol->width()),
                                                            static_cast<float>(m_vol->height())});
        m_postVolumetric->setUniform("volCount", static_cast<int>(volCount));
        std::uint32_t j = 0;
        for (std::uint32_t i = 0; i < m_frame_lights.size() && j < volCount; ++i)
        {
            const DynamicLight& light = m_frame_lights[i];
            const float density = volumetricDensity(light);
            if (density <= 0.0f)
                continue;
            const std::string index = "[" + std::to_string(j) + "]";
            int slot = -1;
            for (std::size_t k = 0; k < entryCount; ++k)
                if (m_point_shadow_maps.entries[k].lightIndex == i)
                    slot = static_cast<int>(k);
            if (const auto* point = std::get_if<PointLight>(&light))
            {
                m_postVolumetric->setUniform(("volPos" + index).c_str(), point->position);
                m_postVolumetric->setUniform(("volDir" + index).c_str(),
                                             core::vec3{0.0f, 0.0f, 0.0f});
                m_postVolumetric->setUniform(("volColor" + index).c_str(), point->color);
                m_postVolumetric->setUniform(("volIntensity" + index).c_str(), point->intensity);
                m_postVolumetric->setUniform(("volRadius" + index).c_str(), point->radius);
                m_postVolumetric->setUniform(("volInnerCos" + index).c_str(), 0.0f);
                m_postVolumetric->setUniform(("volOuterCos" + index).c_str(), -1.0f);
                m_postVolumetric->setUniform(("volIsCone" + index).c_str(), 0);
                m_postVolumetric->setUniform(("volDensity" + index).c_str(), density);
                // Point-style volumes use their paraboloid map when they have one.
                m_postVolumetric->setUniform(("volShadowSlot" + index).c_str(), slot);
            }
            else
            {
                const auto& spot = std::get<SpotLight>(light);
                m_postVolumetric->setUniform(("volPos" + index).c_str(), spot.position);
                m_postVolumetric->setUniform(("volDir" + index).c_str(), spot.direction);
                m_postVolumetric->setUniform(("volColor" + index).c_str(), spot.color);
                m_postVolumetric->setUniform(("volIntensity" + index).c_str(), spot.intensity);
                m_postVolumetric->setUniform(("volRadius" + index).c_str(), spot.range);
                m_postVolumetric->setUniform(("volInnerCos" + index).c_str(),
                                             std::cos(spot.innerAngle));
                m_postVolumetric->setUniform(("volOuterCos" + index).c_str(),
                                             std::cos(spot.outerAngle));
                m_postVolumetric->setUniform(("volIsCone" + index).c_str(), 1);
                m_postVolumetric->setUniform(("volDensity" + index).c_str(), density);
                // No spot shadow maps exist in this tree: cone volumes are
                // unshadowed (documented limitation, decision 25).
                m_postVolumetric->setUniform(("volShadowSlot" + index).c_str(), -1);
            }
            ++j;
        }
        for (std::size_t k = 0; k < entryCount && k < 2; ++k)
        {
            const PointShadowMapEntry& entry = m_point_shadow_maps.entries[k];
            const std::string index = "[" + std::to_string(k) + "]";
            m_postVolumetric->setUniform(("pointLightViews" + index).c_str(), entry.lightView);
            m_postVolumetric->setUniform(("pointLightRange" + index).c_str(),
                                         core::vec2{entry.near, entry.far});
            const float bias01 = m_point_shadow_maps.biasWorld / (entry.far - entry.near);
            m_postVolumetric->setUniform(("pointShadowBias" + index).c_str(), bias01);
        }
        m_postVolumetric->setUniform("pointShadowMapSize",
                                     static_cast<float>(m_point_shadow_maps.mapSize));
        m_fullscreen_quad->drawIndexed();
        m_backend.endRenderPass();
    }

    // SSAO pass: screen-space occlusion from the HDR target's depth texture
    // into the R8 occlusion buffer. Decision 19: the HDR depth is the scene
    // depth in both paths (forward: written by geometry; deferred: copied
    // from the G-buffer above), so there is no path-dependent branch and both
    // paths run identical SSAO on identical depth.
    if (m_config.post.ssao.enabled && m_ssao != nullptr)
    {
        m_backend.beginRenderPass(m_ssao);
        m_backend.bindTexture(m_backend.renderTargetTexture(m_hdr, kDepthAttachment), 0);
        m_postSSAO->use();
        m_postSSAO->setUniform("depthTex", 0);
        m_postSSAO->setUniform("outSize", core::vec2{static_cast<float>(m_ssao->width()),
                                                      static_cast<float>(m_ssao->height())});
        m_postSSAO->setUniform("invProj", core::perspectiveInverse(
            m_config.camera.fovYRad, m_config.camera.aspect, m_config.camera.near, m_config.camera.far));
        m_postSSAO->setUniform("proj", core::perspective(
            m_config.camera.fovYRad, m_config.camera.aspect, m_config.camera.near, m_config.camera.far));
        m_postSSAO->setUniform("radius", m_config.post.ssao.radius);
        m_postSSAO->setUniform("sampleCount", static_cast<int>(m_config.post.ssao.samples));
        m_postSSAO->setUniform("bias", m_config.post.ssao.bias);
        m_fullscreen_quad->drawIndexed();
        m_backend.endRenderPass();
    }

    // DOF pass (6d): bokeh-disc blur of the linear HDR, src -> the other target.
    if (m_config.post.dof.enabled && m_postA != nullptr)
    {
        RenderTarget* dst = (src == m_hdr) ? m_postA : m_hdr;
        m_backend.beginRenderPass(dst);
        m_backend.bindTexture(m_backend.renderTargetTexture(src, 0), 0);
        m_backend.bindTexture(m_backend.renderTargetTexture(src, 1), 1);
        m_backend.bindTexture(m_backend.renderTargetTexture(m_hdr, kDepthAttachment), 2);
        m_postDOF->use();
        m_postDOF->setUniform("srcTex", 0);
        m_postDOF->setUniform("srcAmbient", 1);
        m_postDOF->setUniform("depthTex", 2);
        m_postDOF->setUniform("invProj", core::perspectiveInverse(
            m_config.camera.fovYRad, m_config.camera.aspect, m_config.camera.near, m_config.camera.far));
        m_postDOF->setUniform("outSize", core::vec2{static_cast<float>(dst->width()),
                                                     static_cast<float>(dst->height())});
        m_postDOF->setUniform("tanHalfFovY", std::tan(m_config.camera.fovYRad * 0.5f));
        m_postDOF->setUniform("focusDistance", m_config.post.dof.focusDistance);
        m_postDOF->setUniform("aperture", m_config.post.dof.aperture);
        m_postDOF->setUniform("maxCocPixels", m_config.post.dof.maxCocPixels);
        m_fullscreen_quad->drawIndexed();
        m_backend.endRenderPass();
        src = dst;
    }

    // Bright pass into bloom level 0 (reads the current HDR image, after any
    // MLAA/DOF, so the threshold sees their linear output — decision 22(b)).
    m_backend.beginRenderPass(m_bloom[0]);
    m_backend.bindTexture(m_backend.renderTargetTexture(src, 0), 0);
    m_postBright->use();
    m_postBright->setUniform("srcTex", 0);
    m_postBright->setUniform("outSize", core::vec2{static_cast<float>(m_bloom[0]->width()),
                                                  static_cast<float>(m_bloom[0]->height())});
    m_postBright->setUniform("threshold", m_config.post.bloomThreshold);
    m_fullscreen_quad->drawIndexed();
    m_backend.endRenderPass();

    // Downsample + blur chain: each level is a half-res blur of the previous.
    for (std::size_t level = 1; level < m_bloom.size(); ++level)
    {
        m_backend.beginRenderPass(m_bloom[level]);
        m_backend.bindTexture(m_backend.renderTargetTexture(m_bloom[level - 1], 0), 0);
        m_postBlur->use();
        m_postBlur->setUniform("srcTex", 0);
        m_postBlur->setUniform("outSize", core::vec2{static_cast<float>(m_bloom[level]->width()),
                                                     static_cast<float>(m_bloom[level]->height())});
        m_postBlur->setUniform("direction",
                               core::vec2{level % 2u == 0u ? 1.0f : 0.0f,
                                          level % 2u == 0u ? 0.0f : 1.0f});
        m_fullscreen_quad->drawIndexed();
        m_backend.endRenderPass();
    }

    // Tonemap + compose into the offscreen 8-bit DISPLAY target
    // (display-encoded output; the linear->display boundary sits here,
    // decision 15). 6e: the default framebuffer is not sampleable through the
    // contract, so the chain ends in this target and the present pass below
    // copies it out. Reads the current HDR image `src` (after DOF), the bloom
    // chain and the occlusion.
    m_backend.beginRenderPass(m_display);
    m_backend.clear();
    m_backend.bindTexture(m_backend.renderTargetTexture(src, 0), 0);
    m_backend.bindTexture(m_backend.renderTargetTexture(src, 1), 1);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_bloom.back(), 0), 2);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_ssao, 0), 3);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_vol, 0), 4);
    m_postTonemap->use();
    m_postTonemap->setUniform("hdrTex", 0);
    m_postTonemap->setUniform("hdrAmbient", 1);
    m_postTonemap->setUniform("bloomTex", 2);
    m_postTonemap->setUniform("ssaoTex", 3);
    m_postTonemap->setUniform("volTex", 4);
    m_postTonemap->setUniform("useVolumetric", volCount > 0 && m_vol != nullptr ? 1 : 0);
    m_postTonemap->setUniform("useSSAO",
                              m_config.post.ssao.enabled && m_ssao != nullptr ? 1 : 0);
    m_postTonemap->setUniform("outSize", core::vec2{static_cast<float>(m_display->width()),
                                                     static_cast<float>(m_display->height())});
    m_postTonemap->setUniform("exposure", m_config.post.exposure);
    m_fullscreen_quad->drawIndexed();
    m_backend.endRenderPass();
    RenderTarget* displaySrc = m_display;

    // MLAA pass (6d/6e): morphological edge blend on the DISPLAY-ENCODED
    // image (decision 22 update — the display target is sampleable, so the
    // luminance threshold means the same in shadows and highlights). Reads
    // the display image + the scene depth (still in m_hdr), writes m_displayB.
    if (m_config.post.mlaa.enabled && m_displayB != nullptr)
    {
        m_backend.beginRenderPass(m_displayB);
        m_backend.bindTexture(m_backend.renderTargetTexture(m_display, 0), 0);
        m_backend.bindTexture(m_backend.renderTargetTexture(m_hdr, kDepthAttachment), 1);
        m_postMLAA->use();
        m_postMLAA->setUniform("srcTex", 0);
        m_postMLAA->setUniform("depthTex", 1);
        m_postMLAA->setUniform("outSize", core::vec2{static_cast<float>(m_displayB->width()),
                                                      static_cast<float>(m_displayB->height())});
        m_postMLAA->setUniform("lumThreshold", m_config.post.mlaa.luminanceThreshold);
        m_postMLAA->setUniform("depthThreshold", m_config.post.mlaa.depthThreshold);
        m_fullscreen_quad->drawIndexed();
        m_backend.endRenderPass();
        displaySrc = m_displayB;
    }

    // Present pass (6e): copy the display target onto the default framebuffer
    // with a textured fullscreen draw (PostBlit). swapBuffers (endFrame) then
    // presents exactly what the chain produced; queued readbacks capture it
    // pre-swap from the default framebuffer.
    m_backend.beginRenderPass(nullptr);
    m_backend.bindTexture(m_backend.renderTargetTexture(displaySrc, 0), 0);
    m_postBlit->use();
    m_postBlit->setUniform("srcTex", 0);
    m_fullscreen_quad->drawIndexed();
    m_backend.endRenderPass();
    m_displaySrc = displaySrc;
    m_backend.setDepthTestEnabled(true);  // restore for the next frame's geometry
}

std::uint32_t Renderer::volumetricLightCount() const
{
    std::uint32_t count = 0;
    for (const DynamicLight& light : m_frame_lights)
        if (volumetricDensity(light) > 0.0f)
            ++count;
    return count;
}

void Renderer::renderShadowMapsIfNeeded()
{
    // Shadow passes first (they need the frame's full caster list). Reuses the
    // existing shadow modules unchanged; consumed by both paths since 5c.
    m_shadow_maps_valid = false;
    if (!m_config.shadows || m_pending.empty())
        return;
    const DirectionalLight* sun = nullptr;
    for (const DrawSubmission& submission : m_pending)
        if (submission.sun.has_value())
            sun = &*submission.sun;
    const std::uint32_t maxLights = m_config.path == RenderPath::Deferred
                                        ? m_config.deferredLightCap
                                        : kMaxDynamicLightsPerObject;
    if (sun != nullptr)
        m_shadow_map = renderShadowCascades(m_backend, *sun, m_config.camera, m_pending,
                                            m_config.cascades, maxLights);
    std::vector<DynamicLight> shadowLights;
    for (const DrawSubmission& submission : m_pending)
        for (std::uint32_t i = 0;
             i < submission.shadowLightRequests && i < submission.lights.size(); ++i)
            shadowLights.push_back(submission.lights[i]);
    if (!shadowLights.empty())
        m_point_shadow_maps = renderPointShadowMaps(m_backend, shadowLights, m_pending,
                                                    m_config.pointShadows, maxLights);
    m_shadow_maps_valid =
        m_shadow_map.cascadeCount > 0 || !m_point_shadow_maps.entries.empty();
}

void Renderer::renderForwardFrame()
{
    renderShadowMapsIfNeeded();
    // The shadow passes end on the default framebuffer (endRenderPass), which
    // would capture the lighting submissions when the chain is on. Re-open
    // the lighting pass when shadow passes actually ran, so submissions land
    // on the lighting target — the HDR target with the chain on, the default
    // framebuffer otherwise (6e fix: exposed when the chain became the
    // default; with post off this is the same binding as before).
    if (m_shadow_map.cascadeCount > 0 || !m_point_shadow_maps.entries.empty())
        m_backend.beginRenderPass(lightingTarget());
    for (DrawSubmission& submission : m_pending)
    {
        if (submission.castOnly)
            continue;  // caster only, not drawn
        submission.ambient = m_config.ambient;  // scene term from renderer config
        if (m_shadow_map.cascadeCount > 0 && submission.sun.has_value())
            submission.shadow = &m_shadow_map;
        if (!m_point_shadow_maps.entries.empty())
            submission.pointShadows = &m_point_shadow_maps;
        m_backend.submit(submission);
    }
}

void Renderer::ensureDeferredResources()
{
    if (m_gbuffer == nullptr || m_gbuffer->width() != m_backend.width() ||
        m_gbuffer->height() != m_backend.height())
    {
        if (m_gbuffer != nullptr)
            m_backend.releaseRenderTarget(m_gbuffer);
        RenderTargetDesc desc;
        desc.width = m_backend.width();
        desc.height = m_backend.height();
        desc.colorAttachmentCount = 2;  // albedo+params, normal+specular (documented layout)
        desc.colorFormats[0] = PixelFormat::Rgba8;
        desc.colorFormats[1] = PixelFormat::Rgba8;
        desc.depthAttachment = true;
        desc.depthFormat = PixelFormat::Depth24Stencil8;
        m_gbuffer = m_backend.createRenderTarget(desc);
    }
    if (!m_gbuffer_program)
        m_gbuffer_program = m_backend.createShaderProgram(ShaderProgramId::GBuffer);
    if (!m_deferred_program)
        m_deferred_program = m_backend.createShaderProgram(ShaderProgramId::DeferredLighting);
    ensureFullscreenQuad();
}

void Renderer::renderDeferredFrame()
{
    ensureDeferredResources();
    renderShadowMapsIfNeeded();  // since 5c the deferred path consumes shadows too

    // G-buffer pass: draw each submission unlit into the G-buffer.
    m_backend.beginRenderPass(m_gbuffer);
    m_backend.clear();
    const core::mat4 view =
        core::lookAt(m_config.camera.eye, m_config.camera.target, m_config.camera.up);
    const core::mat4 projection = core::perspective(m_config.camera.fovYRad,
                                                    m_config.camera.aspect,
                                                    m_config.camera.near, m_config.camera.far);
    for (const DrawSubmission& submission : m_pending)
    {
        if (submission.castOnly)
            continue;  // caster only, not drawn into the G-buffer
        const core::mat4 mvp = projection * view * submission.model;
        m_gbuffer_program->use();
        m_gbuffer_program->setUniform("mvp", mvp);
        m_gbuffer_program->setUniform("model", submission.model);
        const SurfaceParams& surface = submission.material.surfaceParams();
        m_gbuffer_program->setUniform("albedo", surface.albedo);
        m_gbuffer_program->setUniform("specular", surface.specular);
        m_gbuffer_program->setUniform("shininess", surface.shininess);
        submission.mesh->drawIndexed();
    }

    // Lighting pass into the lighting target (HDR when post is on, else the
    // default framebuffer), reading the G-buffer and the shadow maps. The
    // fullscreen quad runs with the depth test off (decision 20): it must
    // never write depth, so the HDR target's depth keeps the scene depth
    // copied from the G-buffer below.
    m_backend.beginRenderPass(lightingTarget());
    m_backend.setDepthTestEnabled(false);
    m_backend.clear();
    m_backend.bindTexture(m_backend.renderTargetTexture(m_gbuffer, 0), 0);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_gbuffer, 1), 1);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_gbuffer, kDepthAttachment), 2);
    if (m_shadow_map.cascadeCount > 0)
    {
        for (std::uint32_t i = 0; i < m_shadow_map.cascadeCount; ++i)
            m_backend.bindTexture(
                m_backend.renderTargetTexture(m_shadow_map.targets[i], kDepthAttachment),
                3u + i);
    }
    for (std::size_t k = 0; k < m_point_shadow_maps.entries.size(); ++k)
    {
        const PointShadowMapEntry& entry = m_point_shadow_maps.entries[k];
        m_backend.bindTexture(m_backend.renderTargetTexture(entry.front, kDepthAttachment),
                              7u + 2u * static_cast<std::uint32_t>(k));
        m_backend.bindTexture(m_backend.renderTargetTexture(entry.back, kDepthAttachment),
                              8u + 2u * static_cast<std::uint32_t>(k));
    }

    const core::mat4 invView = core::inverseAffine(view);
    const core::mat4 invProj = core::perspectiveInverse(m_config.camera.fovYRad,
                                                        m_config.camera.aspect,
                                                        m_config.camera.near, m_config.camera.far);

    m_deferred_program->use();
    m_deferred_program->setUniform("gbufAlbedo", 0);
    m_deferred_program->setUniform("gbufNormal", 1);
    m_deferred_program->setUniform("gbufDepth", 2);
    m_deferred_program->setUniform("invProj", invProj);
    m_deferred_program->setUniform("invView", invView);
    m_deferred_program->setUniform("cameraPos", m_config.camera.eye);
    m_deferred_program->setUniform(
        "clearColor", core::vec4{static_cast<float>(m_config.clear_color.r) / 255.0f,
                                 static_cast<float>(m_config.clear_color.g) / 255.0f,
                                 static_cast<float>(m_config.clear_color.b) / 255.0f,
                                 static_cast<float>(m_config.clear_color.a) / 255.0f});
    if (m_frame_sun.has_value())
    {
        m_deferred_program->setUniform("hasSun", 1);
        m_deferred_program->setUniform("sunDirection", core::normalise(m_frame_sun->direction));
        m_deferred_program->setUniform("sunColor", m_frame_sun->color);
        m_deferred_program->setUniform("sunIntensity", m_frame_sun->intensity);
    }
    else
    {
        m_deferred_program->setUniform("hasSun", 0);
    }
    const int lightCount = static_cast<int>(m_frame_lights.size());
    m_deferred_program->setUniform("lightCount", lightCount);
    m_deferred_program->setUniform("ambient", m_config.ambient);
    // Shadow uniforms (same layout as the forward path; units 3+ as bound above).
    if (m_shadow_map.cascadeCount > 0)
    {
        const int count = static_cast<int>(m_shadow_map.cascadeCount);
        m_deferred_program->setUniform("hasShadowMap", 1);
        m_deferred_program->setUniform("cascadeCount", count);
        m_deferred_program->setUniform("shadowBias", m_shadow_map.bias);
        m_deferred_program->setUniform("filterTolerance", m_shadow_map.filterTolerance);
        m_deferred_program->setUniform("shadowMapSize", static_cast<float>(m_shadow_map.mapSize));
        for (int i = 0; i < count; ++i)
        {
            const std::string index = "[" + std::to_string(i) + "]";
            m_deferred_program->setUniform(("shadowMaps" + index).c_str(), 3 + i);
            m_deferred_program->setUniform(("lightMatrices" + index).c_str(),
                                           m_shadow_map.lightMatrices[static_cast<std::size_t>(i)]);
        }
        for (int i = 0; i <= count; ++i)
        {
            const std::string index = "[" + std::to_string(i) + "]";
            m_deferred_program->setUniform(("splitDepths" + index).c_str(),
                                           m_shadow_map.splitDepths[static_cast<std::size_t>(i)]);
        }
    }
    else
    {
        m_deferred_program->setUniform("hasShadowMap", 0);
    }
    if (!m_point_shadow_maps.entries.empty())
    {
        const int entryCount = static_cast<int>(m_point_shadow_maps.entries.size());
        m_deferred_program->setUniform("pointShadowCount", entryCount);
        m_deferred_program->setUniform("pointFilterTolerance", m_point_shadow_maps.filterTolerance);
        m_deferred_program->setUniform("pointShadowMapSize",
                                       static_cast<float>(m_point_shadow_maps.mapSize));
        for (int k = 0; k < entryCount; ++k)
        {
            const PointShadowMapEntry& entry =
                m_point_shadow_maps.entries[static_cast<std::size_t>(k)];
            const std::string index = "[" + std::to_string(k) + "]";
            m_deferred_program->setUniform(("pointShadowFront" + index).c_str(), 7 + 2 * k);
            m_deferred_program->setUniform(("pointShadowBack" + index).c_str(), 8 + 2 * k);
            m_deferred_program->setUniform(("pointLightViews" + index).c_str(), entry.lightView);
            const PointLight& pl =
                std::get<PointLight>(m_frame_lights[entry.lightIndex]);
            m_deferred_program->setUniform(("pointLightPos" + index).c_str(), pl.position);
            m_deferred_program->setUniform(("pointLightRange" + index).c_str(),
                                           core::vec2{entry.near, entry.far});
            const float bias01 = m_point_shadow_maps.biasWorld / (entry.far - entry.near);
            m_deferred_program->setUniform(("pointShadowBias" + index).c_str(), bias01);
        }
        for (int i = 0; i < lightCount; ++i)
        {
            int slot = -1;
            for (int k = 0; k < entryCount; ++k)
                if (m_point_shadow_maps.entries[static_cast<std::size_t>(k)].lightIndex ==
                    static_cast<std::uint32_t>(i))
                    slot = k;
            m_deferred_program->setUniform(("pointLightSlot[" + std::to_string(i) + "]").c_str(),
                                           slot);
        }
    }
    else
    {
        m_deferred_program->setUniform("pointShadowCount", 0);
    }
    for (int i = 0; i < lightCount; ++i)
    {
        const std::string index = "[" + std::to_string(i) + "]";
        std::visit(
            [&](const auto& light) {
                using T = std::decay_t<decltype(light)>;
                if constexpr (std::is_same_v<T, PointLight>)
                {
                    m_deferred_program->setUniform(("lightType" + index).c_str(), 0);
                    m_deferred_program->setUniform(("lightPos" + index).c_str(), light.position);
                    m_deferred_program->setUniform(("lightDir" + index).c_str(),
                                                   core::vec3{0.0f, 0.0f, 0.0f});
                    m_deferred_program->setUniform(("lightColor" + index).c_str(), light.color);
                    m_deferred_program->setUniform(("lightIntensity" + index).c_str(),
                                                   light.intensity);
                    m_deferred_program->setUniform(("lightRange" + index).c_str(), light.radius);
                    m_deferred_program->setUniform(("lightInner" + index).c_str(), 0.0f);
                    m_deferred_program->setUniform(("lightOuter" + index).c_str(), 0.0f);
                }
                else
                {
                    m_deferred_program->setUniform(("lightType" + index).c_str(), 1);
                    m_deferred_program->setUniform(("lightPos" + index).c_str(), light.position);
                    m_deferred_program->setUniform(("lightDir" + index).c_str(),
                                                   core::normalise(light.direction));
                    m_deferred_program->setUniform(("lightColor" + index).c_str(), light.color);
                    m_deferred_program->setUniform(("lightIntensity" + index).c_str(),
                                                   light.intensity);
                    m_deferred_program->setUniform(("lightRange" + index).c_str(), light.range);
                    m_deferred_program->setUniform(("lightInner" + index).c_str(), light.innerAngle);
                    m_deferred_program->setUniform(("lightOuter" + index).c_str(), light.outerAngle);
                }
            },
            m_frame_lights[static_cast<std::size_t>(i)]);
    }
    m_fullscreen_quad->drawIndexed();
    m_backend.setDepthTestEnabled(true);  // restore for the next geometry pass
}

}  // namespace grace::render
