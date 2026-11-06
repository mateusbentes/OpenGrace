#include "grace/render/renderer.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "grace/core/math/camera.hpp"

namespace grace::render {

namespace {

// G-buffer write pass, composed through the contract (the backend only sees
// shader source text, draw calls and pass primitives). Attachment 0: rgb =
// albedo, a = shininess / 1024. Attachment 1: rgb = world normal mapped
// [-1,1] -> [0,1] (8-bit quantization, documented), a = specular luminance.
// Depth goes to the depth attachment.
constexpr const char* kGBufferVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 mvp;
uniform mat4 model;
out vec3 vWorldPos;
out vec3 vNormal;
void main()
{
    vec4 wp = model * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(model) * normal;
    gl_Position = mvp * vec4(position, 1.0);
}
)";

constexpr const char* kGBufferFragmentShader = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
uniform vec3 albedo;
uniform vec3 specular;
uniform float shininess;
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
void main()
{
    outAlbedo = vec4(albedo, shininess / 1024.0);
    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5,
                     dot(specular, vec3(0.299, 0.587, 0.114)));
}
)";

// Fullscreen triangle in NDC.
constexpr const char* kDeferredLightVertexShader = R"(#version 330 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

// Deferred lighting pass: reconstructs world position from the depth attachment
// and applies the sun plus up to kMaxDeferredLights dynamic lights (the
// per-object four-light limit does not apply here; the cap is the shader
// uniform array size and over-cap frames are rejected at endFrame, not
// truncated). Shadow maps are NOT consumed in this increment (documented).
// Fragments still at the cleared depth output the clear colour, so an empty
// frame matches the forward path's cleared framebuffer.
constexpr const char* kDeferredLightFragmentShader = R"(#version 330 core
uniform sampler2D gbufAlbedo;
uniform sampler2D gbufNormal;
uniform sampler2D gbufDepth;
uniform mat4 invProj;
uniform mat4 invView;
uniform vec3 cameraPos;
uniform vec4 clearColor;
uniform int hasSun;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform int lightCount;
uniform int lightType[16];
uniform vec3 lightPos[16];
uniform vec3 lightDir[16];
uniform vec3 lightColor[16];
uniform float lightIntensity[16];
uniform float lightRange[16];
uniform float lightInner[16];
uniform float lightOuter[16];
out vec4 fragment;

float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}

void main()
{
    ivec2 size = textureSize(gbufAlbedo, 0);
    vec2 uv = gl_FragCoord.xy / vec2(size);
    float d = texture(gbufDepth, uv).r;
    if (d >= 1.0) { fragment = vec4(clearColor.rgb, 1.0); return; }  // background
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 view = invProj * clip;
    view /= view.w;
    vec4 world = invView * view;
    world /= world.w;
    vec3 N = normalize(texture(gbufNormal, uv).rgb * 2.0 - 1.0);
    vec3 albedo = texture(gbufAlbedo, uv).rgb;
    float shininess = texture(gbufAlbedo, uv).a * 1024.0;
    float specLum = texture(gbufNormal, uv).a;
    vec3 V = normalize(cameraPos - world.xyz);

    vec3 result = vec3(0.0);
    if (hasSun == 1)
    {
        vec3 L = normalize(-sunDirection);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        result += albedo * sunColor * sunIntensity * ndl
                + vec3(specLum) * sunColor * sunIntensity * spec * ndl;
    }
    for (int i = 0; i < lightCount && i < 16; ++i)
    {
        vec3 toLight = lightPos[i] - world.xyz;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-6);
        float ndl = max(dot(N, L), 0.0);
        float cone = 1.0;
        if (lightType[i] == 1)
        {
            float cosAngle = dot(-L, normalize(lightDir[i]));
            float cosInner = cos(lightInner[i]);
            float cosOuter = cos(lightOuter[i]);
            float t = clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0.0, 1.0);
            cone = t * t * (3.0 - 2.0 * t);
        }
        float atten = rangeAtten(lightIntensity[i], lightRange[i], dist) * cone;
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        result += albedo * lightColor[i] * atten * ndl
                + vec3(specLum) * lightColor[i] * atten * spec * ndl;
    }
    fragment = vec4(clamp(result, 0.0, 1.0), 1.0);
}
)";

}  // namespace

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
    if (m_config.path == RenderPath::Deferred)
    {
        ensureDeferredResources();
        m_backend.beginRenderPass(m_gbuffer);
        m_backend.clear();
    }
    else
    {
        m_backend.beginRenderPass(nullptr);  // default framebuffer
        m_backend.clear();
    }
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

void Renderer::renderForwardFrame()
{
    // Shadow passes first (they need the frame's full caster list), then the
    // lit geometry pass through the backend.
    if (m_config.shadows && !m_pending.empty())
    {
        bool anySun = false;
        for (const DrawSubmission& submission : m_pending)
            if (submission.sun.has_value())
                anySun = true;
        if (anySun)
        {
            const DirectionalLight* sun = nullptr;
            for (const DrawSubmission& submission : m_pending)
                if (submission.sun.has_value())
                    sun = &*submission.sun;
            m_shadow_map =
                renderShadowCascades(m_backend, *sun, m_config.camera, m_pending, m_config.cascades);
        }
        // Point lights that request shadows (leading shadowLightRequests).
        std::vector<DynamicLight> shadowLights;
        for (const DrawSubmission& submission : m_pending)
            for (std::uint32_t i = 0;
                 i < submission.shadowLightRequests && i < submission.lights.size(); ++i)
                shadowLights.push_back(submission.lights[i]);
        if (!shadowLights.empty())
            m_point_shadow_maps = renderPointShadowMaps(m_backend, shadowLights, m_pending,
                                                         m_config.pointShadows);
        m_shadow_maps_valid =
            m_shadow_map.cascadeCount > 0 || !m_point_shadow_maps.entries.empty();
    }

    for (DrawSubmission& submission : m_pending)
    {
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
        m_gbuffer_program = m_backend.createShaderProgram(kGBufferVertexShader,
                                                          kGBufferFragmentShader);
    if (!m_deferred_program)
        m_deferred_program = m_backend.createShaderProgram(kDeferredLightVertexShader,
                                                           kDeferredLightFragmentShader);
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

    // G-buffer pass: the frame was begun on the G-buffer in beginFrame; draw
    // each submission unlit into it.
    const core::mat4 view =
        core::lookAt(m_config.camera.eye, m_config.camera.target, m_config.camera.up);
    const core::mat4 projection = core::perspective(m_config.camera.fovYRad,
                                                    m_config.camera.aspect,
                                                    m_config.camera.near, m_config.camera.far);
    for (const DrawSubmission& submission : m_pending)
    {
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

    // Lighting pass into the default framebuffer, reading the G-buffer.
    m_backend.beginRenderPass(nullptr);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_gbuffer, 0), 0);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_gbuffer, 1), 1);
    m_backend.bindTexture(m_backend.renderTargetTexture(m_gbuffer, kDepthAttachment), 2);

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
