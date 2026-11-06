#include "grace/render/renderer.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "grace/core/math/camera.hpp"

namespace grace::render {


void RendererConfig::validate() const
{
    if (path != RenderPath::Forward && path != RenderPath::Deferred)
        throw std::invalid_argument("renderer config: out-of-range render path");
    camera.validate();
    validateCascadeConfig(cascades);
    validatePointShadowConfig(pointShadows);
    if (deferredLightCap == 0)
        throw std::invalid_argument("renderer config: deferredLightCap must be > 0");
}

Renderer::Renderer(RenderBackend& backend) : m_backend(backend) {}

void Renderer::setConfig(const RendererConfig& config)
{
    config.validate();  // throws std::invalid_argument
    m_config = config;
    releaseShadowMaps();
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
        m_backend.beginRenderPass(nullptr);  // default framebuffer
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
        m_frame_lights.push_back(light);
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
    m_backend.endRenderPass();
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
    for (DrawSubmission& submission : m_pending)
    {
        if (submission.castOnly)
            continue;  // caster only, not drawn
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

    // Lighting pass into the default framebuffer, reading the G-buffer and the
    // shadow maps (units 0-2 G-buffer, 3+ cascade maps, then point maps).
    m_backend.beginRenderPass(nullptr);
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
}

}  // namespace grace::render
