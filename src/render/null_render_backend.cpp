#include "grace/render/null_render_backend.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "grace/render/shadow.hpp"

namespace grace::render {

namespace {

std::string colorText(ClearColor color)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%d,%d,%d,%d", static_cast<int>(color.r),
                  static_cast<int>(color.g), static_cast<int>(color.b),
                  static_cast<int>(color.a));
    return std::string(buffer);
}

std::string pointText(std::uint32_t x, std::uint32_t y)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%u,%u", static_cast<unsigned int>(x),
                  static_cast<unsigned int>(y));
    return std::string(buffer);
}

}  // namespace

// --- NullGpuMesh -----------------------------------------------------------

void NullGpuMesh::upload(const Mesh& mesh)
{
    mesh.validate();  // same rejection behaviour as a real backend
    m_owner.record("upload");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullGpuMesh::drawIndexed() const
{
    m_owner.record("drawIndexed");
    if (!m_owner.ready())
        m_owner.countViolation();
}

// --- NullShaderProgram -----------------------------------------------------

void NullShaderProgram::use()
{
    m_owner.record("use");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullShaderProgram::setUniform(const char* name, float)
{
    m_owner.record(std::string("setUniform(") + name + ")");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullShaderProgram::setUniform(const char* name, int)
{
    m_owner.record(std::string("setUniform(") + name + ")");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullShaderProgram::setUniform(const char* name, const core::vec2&)
{
    m_owner.record(std::string("setUniform(") + name + ")");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullShaderProgram::setUniform(const char* name, const core::vec3&)
{
    m_owner.record(std::string("setUniform(") + name + ")");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullShaderProgram::setUniform(const char* name, const core::vec4&)
{
    m_owner.record(std::string("setUniform(") + name + ")");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullShaderProgram::setUniform(const char* name, const core::mat4&)
{
    m_owner.record(std::string("setUniform(") + name + ")");
    if (!m_owner.ready())
        m_owner.countViolation();
}

void NullShaderProgram::setUniformSampler(const char* name, std::int32_t textureUnit)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "setUniformSampler(%s,%d)", name,
                  static_cast<int>(textureUnit));
    m_owner.record(std::string(buffer));
    if (!m_owner.ready())
        m_owner.countViolation();
}

std::int32_t NullShaderProgram::uniformLocation(const char* name)
{
    // Stable fake location per name, cached like a real backend would.
    return m_cache.location(name, [this](const std::string&) { return m_next_location++; });
}

// --- NullRenderBackend -----------------------------------------------------

NullRenderBackend::NullRenderBackend(std::uint32_t width, std::uint32_t height)
    : m_width(width), m_height(height)
{
    if (width == 0 || height == 0)
        throw std::invalid_argument("NullRenderBackend: width and height must be positive");
}

std::string NullRenderBackend::describe() const
{
    return "null (headless)";
}

void NullRenderBackend::setClearColor(ClearColor color)
{
    record("setClearColor(" + colorText(color) + ")");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_clear_color = color;
}

void NullRenderBackend::setDepthTestEnabled(bool enabled)
{
    record(enabled ? "setDepthTestEnabled(1)" : "setDepthTestEnabled(0)");
    if (m_shutdown)
        ++m_violations;
}

void NullRenderBackend::clear()
{
    record("clear(" + colorText(m_clear_color) + ")");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    // A pass on a volume layer writes that layer: the clear colour is modelled
    // into it, the same way the readback path models the clear colour into the
    // centre pixel. Without this the null backend could not tell a pass that
    // wrote its layer from one that wrote nothing, and the ownership rule —
    // releasing the target leaves the volume intact — would be untestable here.
    NullRenderTarget* bound = findTarget(m_bound_target);
    if (bound != nullptr && bound->isLayerTarget())
    {
        NullTexture3DView* volume = bound->layerVolume();
        std::vector<std::byte>& slice = volume->slice(bound->layer());
        const std::size_t texel = bytesPerPixel(volume->format());
        for (std::size_t i = 0; i + texel <= slice.size(); i += texel)
        {
            slice[i] = static_cast<std::byte>(m_clear_color.r);
            if (texel > 1u)
                slice[i + 1u] = static_cast<std::byte>(m_clear_color.g);
            if (texel > 2u)
                slice[i + 2u] = static_cast<std::byte>(m_clear_color.b);
            if (texel > 3u)
                slice[i + 3u] = static_cast<std::byte>(m_clear_color.a);
        }
    }
}

void NullRenderBackend::shutdown()
{
    record("shutdown");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_shutdown = true;
}

std::unique_ptr<ShaderProgram> NullRenderBackend::createShaderProgram(ShaderProgramId id)
{
    const char* name = "unknown";
    switch (id)
    {
    case ShaderProgramId::FlatUnlit: name = "FlatUnlit"; break;
    case ShaderProgramId::TestBrokenVertex: name = "TestBrokenVertex"; break;
    case ShaderProgramId::TestBrokenFragment: name = "TestBrokenFragment"; break;
    case ShaderProgramId::ForwardLighting: name = "ForwardLighting"; break;
    case ShaderProgramId::ShadowDepth: name = "ShadowDepth"; break;
    case ShaderProgramId::ParaboloidDepth: name = "ParaboloidDepth"; break;
    case ShaderProgramId::GBuffer: name = "GBuffer"; break;
    case ShaderProgramId::DeferredLighting: name = "DeferredLighting"; break;
    case ShaderProgramId::PostBright: name = "PostBright"; break;
    case ShaderProgramId::PostBlur: name = "PostBlur"; break;
    case ShaderProgramId::PostTonemap: name = "PostTonemap"; break;
    case ShaderProgramId::PostSSAO: name = "PostSSAO"; break;
    case ShaderProgramId::PostMLAA: name = "PostMLAA"; break;
    case ShaderProgramId::PostDOF: name = "PostDOF"; break;
    case ShaderProgramId::PostBlit: name = "PostBlit"; break;
    case ShaderProgramId::PostVolumetric: name = "PostVolumetric"; break;
    case ShaderProgramId::VolumeInject: name = "VolumeInject"; break;
    case ShaderProgramId::VolumePropagate: name = "VolumePropagate"; break;
    }
    record(std::string("createShaderProgram(") + name + ")");
    if (m_shutdown)
    {
        ++m_violations;
        throw std::logic_error("NullRenderBackend::createShaderProgram: backend is shut down");
    }
    return std::make_unique<NullShaderProgram>(*this);
}

std::unique_ptr<GpuMesh> NullRenderBackend::createGpuMesh()
{
    record("createGpuMesh");
    if (m_shutdown)
    {
        ++m_violations;
        throw std::logic_error("NullRenderBackend::createGpuMesh: backend is shut down");
    }
    return std::make_unique<NullGpuMesh>(*this);
}

void NullRenderBackend::draw(GpuMesh& mesh, ShaderProgram& program)
{
    record("draw");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    program.use();
    mesh.drawIndexed();
}

void NullRenderBackend::submit(const DrawSubmission& submission)
{
    submission.validate();  // same rejection behaviour as a real backend
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer),
                  "submit(lights=%u,sun=%d,shadow=%d,pointShadows=%d,volume=%d)",
                  static_cast<unsigned int>(submission.lights.size()),
                  submission.sun.has_value() ? 1 : 0,
                  submission.shadow != nullptr ? 1 : 0,
                  submission.pointShadows != nullptr ? 1 : 0,
                  submission.volume != nullptr ? 1 : 0);
    record(std::string(buffer));
    if (submission.shadowLightRequests > kMaxShadowCastingPointLights)
        ++m_violations;  // documented: over-budget request, excess lights unshadowed
    if (m_shutdown)
        ++m_violations;
}

NullRenderTarget* NullRenderBackend::findTarget(RenderTarget* target) const
{
    for (const auto& candidate : m_targets)
        if (candidate.get() == target)
            return candidate.get();
    return nullptr;
}

RenderTarget* NullRenderBackend::createRenderTarget(const RenderTargetDesc& desc)
{
    validateRenderTargetDesc(desc);  // throws, enforces the attachment cap
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "createRenderTarget(%ux%u,color=%u,depth=%d)",
                  static_cast<unsigned int>(desc.width), static_cast<unsigned int>(desc.height),
                  static_cast<unsigned int>(desc.colorAttachmentCount),
                  desc.depthAttachment ? 1 : 0);
    record(std::string(buffer));
    if (m_shutdown)
    {
        ++m_violations;
        throw std::logic_error("NullRenderBackend::createRenderTarget: backend is shut down");
    }
    auto target = std::make_unique<NullRenderTarget>(desc);
    NullRenderTarget* raw = target.get();
    m_targets.push_back(std::move(target));
    return raw;
}

void NullRenderBackend::beginRenderPass(RenderTarget* target)
{
    if (target == nullptr)
    {
        record("beginRenderPass(default)");
        if (m_shutdown)
            ++m_violations;
        else
            m_bound_target = nullptr;
        return;
    }
    NullRenderTarget* known = findTarget(target);
    if (known == nullptr || known->released)
    {
        record("beginRenderPass(unknown)");
        ++m_violations;
        return;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "beginRenderPass(%ux%u)",
                  static_cast<unsigned int>(known->width()),
                  static_cast<unsigned int>(known->height()));
    record(std::string(buffer));
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_bound_target = known;  // beginning a pass while another is bound replaces it
}

void NullRenderBackend::endRenderPass()
{
    record("endRenderPass");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_bound_target = nullptr;
}

void NullRenderBackend::setCamera(const CameraParams& camera)
{
    camera.validate();  // same rejection as a real backend
    record("setCamera");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_camera = camera;
}

void NullRenderBackend::releaseRenderTarget(RenderTarget* target)
{
    record("releaseRenderTarget");
    NullRenderTarget* known = findTarget(target);
    if (known == nullptr || known->released || m_shutdown)
    {
        ++m_violations;
        return;
    }
    known->released = true;
    if (m_bound_target == known)
        m_bound_target = nullptr;
}

TextureView* NullRenderBackend::renderTargetTexture(RenderTarget* target)
{
    record("renderTargetTexture");
    NullRenderTarget* known = findTarget(target);
    if (known == nullptr || known->released || m_shutdown || m_bound_target == known)
    {
        // Unknown, released, shut down, or bound for rendering (feedback loop).
        ++m_violations;
        return nullptr;
    }
    if (known->hasColorAttachment())
        return &known->colorViews[0];
    if (known->hasDepthAttachment())
        return &known->depthView;
    ++m_violations;
    return nullptr;
}

TextureView* NullRenderBackend::renderTargetTexture(RenderTarget* target, std::uint32_t attachment)
{
    record("renderTargetTexture");
    NullRenderTarget* known = findTarget(target);
    if (known == nullptr || known->released || m_shutdown || m_bound_target == known)
    {
        ++m_violations;
        return nullptr;
    }
    if (attachment == kDepthAttachment)
    {
        if (!known->hasDepthAttachment())
        {
            ++m_violations;
            return nullptr;
        }
        return &known->depthView;
    }
    if (attachment < known->colorViews.size())
        return &known->colorViews[attachment];
    ++m_violations;
    return nullptr;
}

void NullRenderBackend::copyDepthAttachment(RenderTarget* source, RenderTarget* destination)
{
    record("copyDepthAttachment");
    NullRenderTarget* src = findTarget(source);
    NullRenderTarget* dst = findTarget(destination);
    if (m_shutdown || src == nullptr || dst == nullptr || src->released || dst->released ||
        !src->hasDepthAttachment() || !dst->hasDepthAttachment() || source == destination ||
        src->width() != dst->width() || src->height() != dst->height() ||
        m_bound_target == src || m_bound_target == dst)
    {
        ++m_violations;
        return;
    }
    // Null backend: depth contents are not modelled; the copy is a no-op.
}

void NullRenderBackend::bindTexture(const TextureView* view, std::uint32_t unit)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "bindTexture(unit=%u,depth=%d)",
                  static_cast<unsigned int>(unit), view != nullptr && view->isDepth() ? 1 : 0);
    record(std::string(buffer));
    // A volume is not a 2D attachment: the OpenGL backend rejects it by the
    // dynamic type, so the null backend rejects it by the layer count, and both
    // count the same violation.
    if (view == nullptr || view->depth() != 1u || m_shutdown)
        ++m_violations;
}

NullTexture3DView* NullRenderBackend::findVolume(TextureView* view) const
{
    for (const auto& volume : m_volumes)
        if (volume.get() == view)
            return volume.get();
    return nullptr;
}

void NullRenderBackend::releaseTexture3D(TextureView* view)
{
    record("releaseTexture3D");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    // Only a volume this backend created may be released, and only once.
    for (const auto& volume : m_volumes)
    {
        if (volume.get() == view)
        {
            if (volume->released)
                ++m_violations;
            else
                volume->released = true;
            return;
        }
    }
    ++m_violations;
}

TextureView* NullRenderBackend::createTexture3D(const Texture3DDesc& desc)
{
    validateTexture3DDesc(desc);  // throws, the same rejection a real backend makes
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "createTexture3D(%ux%ux%u)",
                  static_cast<unsigned int>(desc.width), static_cast<unsigned int>(desc.height),
                  static_cast<unsigned int>(desc.depth));
    record(std::string(buffer));
    if (m_shutdown)
    {
        ++m_violations;
        throw std::logic_error("NullRenderBackend::createTexture3D: backend is shut down");
    }
    auto volume = std::make_unique<NullTexture3DView>(desc);
    NullTexture3DView* raw = volume.get();
    m_volumes.push_back(std::move(volume));
    return raw;
}

void NullRenderBackend::bindTexture3D(const TextureView* view, std::uint32_t unit)
{
    char buffer[56];
    std::snprintf(buffer, sizeof(buffer), "bindTexture3D(unit=%u,depth=%u)",
                  static_cast<unsigned int>(unit),
                  static_cast<unsigned int>(view != nullptr ? view->depth() : 0u));
    record(std::string(buffer));
    // A single-layer view is a 2D attachment, not a volume: the distinction a
    // real backend makes by binding GL_TEXTURE_3D instead of GL_TEXTURE_2D.
    if (view == nullptr || view->depth() < 2u || m_shutdown)
        ++m_violations;
}

void NullRenderBackend::fillTexture3D(TextureView* view, std::uint32_t layer, const void* data,
                                      std::size_t sizeBytes)
{
    NullTexture3DView* volume = findVolume(view);
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "fillTexture3D(layer=%u,bytes=%zu)",
                  static_cast<unsigned int>(layer), sizeBytes);
    record(std::string(buffer));
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    if (volume == nullptr)
        throw std::invalid_argument("fillTexture3D: view is not a 3D texture");
    if (layer >= volume->depth() || sizeBytes != volume->sliceBytes())
        throw std::invalid_argument(
            "fillTexture3D: layer out of range or byte count does not match the volume");
    const auto* bytes = static_cast<const std::byte*>(data);
    volume->slice(layer).assign(bytes, bytes + sizeBytes);
}

bool NullRenderBackend::readTexture3D(const TextureView* view, std::uint32_t layer, void* data,
                                      std::size_t sizeBytes)
{
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "readTexture3D(layer=%u,bytes=%zu)",
                  static_cast<unsigned int>(layer), sizeBytes);
    record(std::string(buffer));
    if (m_shutdown)
    {
        ++m_violations;
        return false;
    }
    // findVolume takes a mutable pointer; this lookup only reads.
    NullTexture3DView* volume = findVolume(const_cast<TextureView*>(view));
    if (volume == nullptr || layer >= volume->depth() || sizeBytes != volume->sliceBytes())
    {
        ++m_violations;
        return false;
    }
    const std::vector<std::byte>& slice = volume->slice(layer);
    std::memcpy(data, slice.data(), sizeBytes);
    return true;
}

RenderTarget* NullRenderBackend::createVolumeLayerTarget(TextureView* volume, std::uint32_t layer)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "createVolumeLayerTarget(layer=%u)",
                  static_cast<unsigned int>(layer));
    record(std::string(buffer));
    if (m_shutdown)
    {
        ++m_violations;
        throw std::logic_error("NullRenderBackend::createVolumeLayerTarget: backend is shut down");
    }
    NullTexture3DView* known = findVolume(volume);
    if (known == nullptr)
        throw std::invalid_argument(
            "createVolumeLayerTarget: view is not a 3D texture of this backend");
    VolumeLayerDesc desc;
    desc.volume = known->desc();
    desc.layer = layer;
    validateVolumeLayerDesc(desc);  // throws, the same rule the OpenGL backend applies
    auto target = std::make_unique<NullRenderTarget>(known, layer);
    NullRenderTarget* raw = target.get();
    m_targets.push_back(std::move(target));
    return raw;
}

void NullRenderBackend::queueReadback(std::uint32_t x, std::uint32_t y)
{
    record("queueReadback(" + pointText(x, y) + ")");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_readback_queue.emplace_back(x, y);
}

void NullRenderBackend::swapBuffers()
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    // No event recorded: the event log is the frame-assembly trace that the
    // application and renderer suites assert on, and presenting is not part of
    // the assembly this backend observes.
}

void NullRenderBackend::readback()
{
    record("readback");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    // Documented null values: the current clear colour everywhere, depth 1.0,
    // stencil 0.
    m_has_center = true;
    m_center_pixel = m_clear_color;
    m_last_readback.clear();
    for (const auto& point : m_readback_queue)
    {
        ReadbackSample sample{};
        sample.x = point.first;
        sample.y = point.second;
        sample.color = m_clear_color;
        sample.depth = 1.0f;
        sample.stencil = 0;
        m_last_readback.push_back(sample);
    }
    m_readback_queue.clear();
}

void NullRenderBackend::record(std::string event)
{
    m_events.push_back(std::move(event));
}

}  // namespace grace::render
