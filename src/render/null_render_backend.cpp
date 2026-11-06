#include "grace/render/null_render_backend.hpp"

#include <cstdio>
#include <stdexcept>

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

void NullRenderBackend::clear()
{
    record("clear(" + colorText(m_clear_color) + ")");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    // Mirror the real backends: a queued readback is captured with the clear.
    if (!m_readback_queue.empty())
        readback();
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

std::unique_ptr<ShaderProgram> NullRenderBackend::createShaderProgram(
    const std::string& vertexSource, const std::string& fragmentSource)
{
    record("createShaderProgram");
    const ShaderSources sources{vertexSource, fragmentSource};
    validateShaderSources(sources);  // same rejection as a real compiler path
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
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "submit(lights=%u,sun=%d,shadow=%d)",
                  static_cast<unsigned int>(submission.lights.size()),
                  submission.sun.has_value() ? 1 : 0,
                  submission.shadow != nullptr ? 1 : 0);
    record(std::string(buffer));
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
    if (desc.width == 0 || desc.height == 0)
        throw std::invalid_argument("render target: width and height must be positive");
    if (!desc.colorAttachment && !desc.depthAttachment)
        throw std::invalid_argument(
            "render target: incomplete (neither a colour nor a depth attachment)");
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "createRenderTarget(%ux%u,color=%d,depth=%d)",
                  static_cast<unsigned int>(desc.width), static_cast<unsigned int>(desc.height),
                  desc.colorAttachment ? 1 : 0, desc.depthAttachment ? 1 : 0);
    record(std::string(buffer));
    if (m_shutdown)
    {
        ++m_violations;
        throw std::logic_error("NullRenderBackend::createRenderTarget: backend is shut down");
    }
    auto target = std::make_unique<NullRenderTarget>(desc);
    target->colorView = NullTextureView(desc.width, desc.height, desc.colorFormat, false);
    target->depthView = NullTextureView(desc.width, desc.height, desc.depthFormat, true);
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
        return &known->colorView;
    if (known->hasDepthAttachment())
        return &known->depthView;
    ++m_violations;
    return nullptr;
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
