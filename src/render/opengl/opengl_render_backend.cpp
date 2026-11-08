#include "grace/render/opengl_render_backend.hpp"

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "grace/core/math/camera.hpp"
#include "grace/core/math/transform.hpp"
#include "grace/render/material.hpp"
#include "grace/render/opengl/gl_loader.hpp"
#include "grace/render/shadow.hpp"

#include "opengl_mesh.hpp"
#include "opengl_render_target.hpp"
#include "opengl_shader.hpp"

namespace grace::render {

namespace {

constexpr int kRequestedMajor = 3;
constexpr int kRequestedMinor = 3;

constexpr float kPi = 3.14159265358979323846f;

// Diagnostic triangle shaders, built on the generic shader/material path.
std::string glErrorText(GLenum error)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%04X", static_cast<unsigned int>(error));
    return std::string(buffer);
}

std::string asString(const GLubyte* text)
{
    if (text == nullptr)
        return std::string();
    return std::string(reinterpret_cast<const char*>(text));
}

/// Maps the platform-neutral ClearBuffer bitmask onto GL clear bits.
GLbitfield toGlMask(std::uint32_t mask)
{
    GLbitfield gl_mask = 0;
    if ((mask & static_cast<std::uint32_t>(ClearBuffer::Color)) != 0u)
        gl_mask |= GL_COLOR_BUFFER_BIT;
    if ((mask & static_cast<std::uint32_t>(ClearBuffer::Depth)) != 0u)
        gl_mask |= GL_DEPTH_BUFFER_BIT;
    if ((mask & static_cast<std::uint32_t>(ClearBuffer::Stencil)) != 0u)
        gl_mask |= GL_STENCIL_BUFFER_BIT;
    return gl_mask;
}

opengl::GlRenderTarget* findGlTarget(
    std::vector<std::unique_ptr<opengl::GlRenderTarget>>& targets, RenderTarget* target)
{
    for (auto& candidate : targets)
        if (candidate.get() == target)
            return candidate.get();
    return nullptr;
}

opengl::GlTexture3DView* findGlVolume(
    std::vector<std::unique_ptr<opengl::GlTexture3DView>>& volumes, const TextureView* view)
{
    for (auto& candidate : volumes)
        if (candidate.get() == view)
            return candidate.get();
    return nullptr;
}

void parseVersion(const std::string& version, int& major, int& minor)
{
    major = 0;
    minor = 0;
    std::size_t pos = 0;
    auto readInt = [&](int& out) {
        int value = 0;
        bool any = false;
        while (pos < version.size() && version[pos] >= '0' && version[pos] <= '9')
        {
            value = value * 10 + (version[pos] - '0');
            ++pos;
            any = true;
        }
        out = value;
        return any;
    };
    if (readInt(major) && pos < version.size() && version[pos] == '.')
    {
        ++pos;
        static_cast<void>(readInt(minor));
    }
}

}  // namespace

struct OpenGLRenderBackend::Impl
{
    enum class State
    {
        Created,
        Initialised,
        Shutdown
    };

    OpenGLBackendConfig config;
    opengl::GlLoader gl;

    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    State state = State::Created;
    bool used_fallback = false;
    int context_major = 0;
    int context_minor = 0;
    std::string version_string;
    std::string vendor;
    std::string renderer;

    ClearColor clear_color{0, 0, 0, 255};
    float clear_depth = 1.0f;
    std::int32_t clear_stencil = 0;
    std::uint32_t clear_mask = static_cast<std::uint32_t>(
        ClearBuffer::Color | ClearBuffer::Depth | ClearBuffer::Stencil);
    // Device state (decision 20): when false, passes run without a depth
    // test and without depth writes, so fullscreen passes never overwrite a
    // target's scene depth. Default true; geometry passes rely on it.
    bool depth_test = true;

    bool has_center = false;
    ClearColor center_pixel{0, 0, 0, 0};
    float center_depth = 0.0f;
    std::int32_t center_stencil = 0;

    std::vector<std::pair<std::uint32_t, std::uint32_t>> readback_queue;
    std::vector<ReadbackSample> last_readback;

    std::uint64_t violations = 0;
    std::uint64_t gl_errors = 0;
    std::string last_error;

    // Diagnostic triangle resources, built on the render foundation
    // (math + mesh + shader + material).
    std::unique_ptr<GpuMesh> tri_mesh;
    std::shared_ptr<ShaderProgram> tri_shader;
    std::shared_ptr<Material> tri_material;
    bool triangle_ready = false;

    // Forward lighting pass program (built lazily on first submit).
    std::shared_ptr<ShaderProgram> lighting_program;

    // Render targets: backend-owned registry. Handles stay alive until
    // shutdown so use-after-release is detected safely.
    std::vector<std::unique_ptr<opengl::GlRenderTarget>> targets;
    opengl::GlRenderTarget* bound_target = nullptr;

    // 3D textures: backend-owned registry, same lifetime rule as the targets.
    std::vector<std::unique_ptr<opengl::GlTexture3DView>> volumes;

    // Camera used by submit()/draw() (documented diagnostic default).
    CameraParams camera;

    bool ready() const { return state == State::Initialised; }

    void drainGlErrors(const char* where)
    {
        GLenum error = gl.glGetError();
        while (error != GL_NO_ERROR)
        {
            ++gl_errors;
            last_error = "GL error " + glErrorText(error) + " at " + where;
            error = gl.glGetError();
        }
    }
};

OpenGLRenderBackend::OpenGLRenderBackend(OpenGLBackendConfig config)
    : m_impl(std::make_unique<Impl>())
{
    if (config.width == 0 || config.height == 0)
        throw std::invalid_argument(
            "OpenGLRenderBackend: width and height must be positive");
    m_impl->config = config;
    // Documented diagnostic default camera: eye (0,0,1) at the origin, up +Y,
    // fovY 90 degrees, aspect from the frame size.
    m_impl->camera.aspect =
        static_cast<float>(config.width) / static_cast<float>(config.height);
}

OpenGLRenderBackend::~OpenGLRenderBackend()
{
    if (m_impl->state == Impl::State::Initialised)
        shutdownImpl(false);
}

void OpenGLRenderBackend::initialise()
{
    Impl& impl = *m_impl;
    if (impl.state != Impl::State::Created)
        throw std::logic_error("OpenGLRenderBackend::initialise: already initialised");

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    const int width = static_cast<int>(impl.config.width);
    const int height = static_cast<int>(impl.config.height);
    Uint32 window_flags = static_cast<Uint32>(SDL_WINDOW_OPENGL);
    if (impl.config.hidden)
        window_flags |= static_cast<Uint32>(SDL_WINDOW_HIDDEN);

    // Primary request: OpenGL 3.3 core profile.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, kRequestedMajor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, kRequestedMinor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);  // so readback can verify alpha too

    impl.window = SDL_CreateWindow("OpenGrace", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, width, height, window_flags);
    if (impl.window != nullptr)
        impl.context = SDL_GL_CreateContext(impl.window);

    if (impl.context == nullptr)
    {
        // Fallback: OpenGL 3.0 without a core-profile flag (SDL2 has no
        // per-attribute reset, so request the compatibility profile, which a
        // 3.0 context satisfies).
        impl.used_fallback = true;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        if (impl.window == nullptr)
            impl.window = SDL_CreateWindow("OpenGrace", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, width, height,
                                           window_flags);
        if (impl.window != nullptr)
            impl.context = SDL_GL_CreateContext(impl.window);
    }

    if (impl.context == nullptr)
    {
        const std::string message =
            std::string("SDL_GL_CreateContext failed (no OpenGL 3.3 core or 3.0 context "
                        "available): ") +
            SDL_GetError();
        if (impl.window != nullptr)
        {
            SDL_DestroyWindow(impl.window);
            impl.window = nullptr;
        }
        SDL_Quit();
        throw std::runtime_error(message);
    }

    SDL_GL_MakeCurrent(impl.window, impl.context);
    SDL_GL_SetSwapInterval(impl.config.swap_interval);

    std::string missing;
    if (!impl.gl.load(&SDL_GL_GetProcAddress, missing))
    {
        const std::string message =
            "OpenGL loader: could not resolve symbol '" + missing + "'";
        SDL_GL_DeleteContext(impl.context);
        impl.context = nullptr;
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        SDL_Quit();
        throw std::runtime_error(message);
    }

    impl.version_string = asString(impl.gl.glGetString(GL_VERSION));
    impl.vendor = asString(impl.gl.glGetString(GL_VENDOR));
    impl.renderer = asString(impl.gl.glGetString(GL_RENDERER));
    parseVersion(impl.version_string, impl.context_major, impl.context_minor);

    impl.state = Impl::State::Initialised;
    impl.drainGlErrors("initialise");
}

std::string OpenGLRenderBackend::describe() const
{
    const Impl& impl = *m_impl;
    if (impl.state == Impl::State::Created)
        return "opengl (not initialised)";
    return "opengl " + std::to_string(impl.context_major) + "." +
           std::to_string(impl.context_minor) +
           (impl.used_fallback ? " compatibility" : " core") + "; vendor=" + impl.vendor +
           "; renderer=" + impl.renderer;
}

void OpenGLRenderBackend::setClearColor(ClearColor color)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        return;
    }
    m_impl->clear_color = color;
}

ClearColor OpenGLRenderBackend::clearColor() const
{
    return m_impl->clear_color;
}

void OpenGLRenderBackend::clear()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }

    impl.gl.glClearColor(static_cast<GLfloat>(impl.clear_color.r) / 255.0f,
                         static_cast<GLfloat>(impl.clear_color.g) / 255.0f,
                         static_cast<GLfloat>(impl.clear_color.b) / 255.0f,
                         static_cast<GLfloat>(impl.clear_color.a) / 255.0f);
    impl.gl.glClearDepth(static_cast<GLdouble>(impl.clear_depth));
    impl.gl.glClearStencil(static_cast<GLint>(impl.clear_stencil));
    impl.gl.glClear(toGlMask(impl.clear_mask));

    if (impl.config.draw_triangle)
        drawTriangle();

    impl.drainGlErrors("clear");
}

void OpenGLRenderBackend::swapBuffers()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    // Readback happens before the swap: the back buffer contents are undefined
    // after SDL_GL_SwapWindow.
    if (impl.config.readback_center || !impl.readback_queue.empty())
        readback();

    SDL_GL_SwapWindow(impl.window);
    impl.drainGlErrors("swapBuffers");
}

std::uint32_t OpenGLRenderBackend::width() const
{
    return m_impl->config.width;
}

std::uint32_t OpenGLRenderBackend::height() const
{
    return m_impl->config.height;
}

void OpenGLRenderBackend::shutdown()
{
    shutdownImpl(true);
}

void OpenGLRenderBackend::shutdownImpl(bool countViolation)
{
    Impl& impl = *m_impl;
    if (impl.state == Impl::State::Shutdown)
    {
        // Call after shutdown: counted as a violation, but safe (no-op).
        if (countViolation)
            ++impl.violations;
        return;
    }
    if (impl.state != Impl::State::Initialised)
    {
        // Call before initialisation: counted as a violation, no-op.
        if (countViolation)
            ++impl.violations;
        return;
    }

    // Release the foundation objects while the context is still current.
    impl.tri_mesh.reset();
    impl.tri_shader.reset();
    impl.tri_material.reset();
    impl.triangle_ready = false;
    impl.lighting_program.reset();
    impl.bound_target = nullptr;
    for (auto& target : impl.targets)
        target->release();
    impl.targets.clear();
    impl.volumes.clear();  // GL objects deleted while the context is current
    impl.drainGlErrors("shutdown");
    if (impl.context != nullptr)
    {
        SDL_GL_DeleteContext(impl.context);
        impl.context = nullptr;
    }
    if (impl.window != nullptr)
    {
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
    }
    SDL_Quit();
    impl.state = Impl::State::Shutdown;
}

std::uint64_t OpenGLRenderBackend::violationCount() const noexcept
{
    return m_impl->violations;
}

std::uint64_t OpenGLRenderBackend::glErrorCount() const noexcept
{
    return m_impl->gl_errors;
}

std::string OpenGLRenderBackend::lastError() const noexcept
{
    return m_impl->last_error;
}

bool OpenGLRenderBackend::hasCenterPixel() const noexcept
{
    return m_impl->has_center;
}

ClearColor OpenGLRenderBackend::centerPixel() const noexcept
{
    return m_impl->center_pixel;
}

float OpenGLRenderBackend::centerDepth() const noexcept
{
    return m_impl->center_depth;
}

std::int32_t OpenGLRenderBackend::centerStencil() const noexcept
{
    return m_impl->center_stencil;
}

void OpenGLRenderBackend::queueReadback(std::uint32_t x, std::uint32_t y)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        return;
    }
    m_impl->readback_queue.emplace_back(x, y);
}

const std::vector<ReadbackSample>& OpenGLRenderBackend::lastReadback() const noexcept
{
    return m_impl->last_readback;
}

void OpenGLRenderBackend::readback()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    readCenter();
    captureQueuedReadbacks();
}

ClearColor OpenGLRenderBackend::readPixel(std::uint32_t x, std::uint32_t y)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return ClearColor{0, 0, 0, 0};
    }
    // Reading colour from a target without a colour attachment (e.g. a
    // depth-only pass target) is invalid: skip the GL call.
    if (impl.bound_target != nullptr && !impl.bound_target->hasColorAttachment())
        return ClearColor{0, 0, 0, 0};
    GLubyte pixel[4] = {0, 0, 0, 0};
    impl.gl.glReadPixels(static_cast<GLint>(x), static_cast<GLint>(y), 1, 1, GL_RGBA,
                         GL_UNSIGNED_BYTE, pixel);
    impl.drainGlErrors("readPixel");
    return ClearColor{pixel[0], pixel[1], pixel[2], pixel[3]};
}

float OpenGLRenderBackend::readDepth(std::uint32_t x, std::uint32_t y)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return 0.0f;
    }
    if (impl.bound_target != nullptr && !impl.bound_target->hasDepthAttachment())
        return 0.0f;
    GLfloat depth = 0.0f;
    impl.gl.glReadPixels(static_cast<GLint>(x), static_cast<GLint>(y), 1, 1,
                         GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    impl.drainGlErrors("readDepth");
    return depth;
}

std::int32_t OpenGLRenderBackend::readStencilAt(std::uint32_t x, std::uint32_t y)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return 0;
    }
    if (impl.bound_target != nullptr && !impl.bound_target->hasDepthAttachment())
        return 0;
    GLubyte stencil = 0;
    impl.gl.glReadPixels(static_cast<GLint>(x), static_cast<GLint>(y), 1, 1, GL_STENCIL_INDEX,
                         GL_UNSIGNED_BYTE, &stencil);
    impl.drainGlErrors("readStencilAt");
    return static_cast<std::int32_t>(stencil);
}

void OpenGLRenderBackend::captureQueuedReadbacks()
{
    Impl& impl = *m_impl;
    impl.last_readback.clear();
    for (const auto& point : impl.readback_queue)
    {
        ReadbackSample sample{};
        sample.x = point.first;
        sample.y = point.second;
        sample.color = readPixel(point.first, point.second);
        sample.depth = readDepth(point.first, point.second);
        sample.stencil = readStencilAt(point.first, point.second);
        impl.last_readback.push_back(sample);
    }
    impl.readback_queue.clear();
}

void OpenGLRenderBackend::setClearBuffers(std::uint32_t mask) noexcept
{
    m_impl->clear_mask = mask;
}

void OpenGLRenderBackend::setClearDepth(float depth) noexcept
{
    m_impl->clear_depth = depth;
}

void OpenGLRenderBackend::setClearStencil(std::int32_t stencil) noexcept
{
    m_impl->clear_stencil = stencil;
}

int OpenGLRenderBackend::contextMajor() const noexcept
{
    return m_impl->context_major;
}

int OpenGLRenderBackend::contextMinor() const noexcept
{
    return m_impl->context_minor;
}

bool OpenGLRenderBackend::usedFallbackContext() const noexcept
{
    return m_impl->used_fallback;
}

std::string OpenGLRenderBackend::glVersionString() const noexcept
{
    return m_impl->version_string;
}

std::string OpenGLRenderBackend::glVendor() const noexcept
{
    return m_impl->vendor;
}

std::string OpenGLRenderBackend::glRenderer() const noexcept
{
    return m_impl->renderer;
}

void OpenGLRenderBackend::readCenter()
{
    Impl& impl = *m_impl;
    const std::uint32_t cx = impl.config.width / 2u;
    const std::uint32_t cy = impl.config.height / 2u;
    impl.center_pixel = readPixel(cx, cy);
    impl.center_depth = readDepth(cx, cy);
    impl.center_stencil = readStencilAt(cx, cy);
    impl.has_center = true;
}

std::unique_ptr<ShaderProgram> OpenGLRenderBackend::createShaderProgram(ShaderProgramId id)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        throw std::logic_error("OpenGLRenderBackend::createShaderProgram: not initialised");
    }
    return std::make_unique<opengl::GlShaderProgram>(m_impl->gl, id);
}

std::unique_ptr<GpuMesh> OpenGLRenderBackend::createGpuMesh()
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        throw std::logic_error("OpenGLRenderBackend::createGpuMesh: not initialised");
    }
    return std::make_unique<opengl::GlMesh>(m_impl->gl);
}

void OpenGLRenderBackend::applyDepthState()
{
    Impl& impl = *m_impl;
    if (impl.depth_test)
    {
        impl.gl.glEnable(GL_DEPTH_TEST);
        impl.gl.glDepthFunc(GL_LESS);
        impl.gl.glDepthMask(GL_TRUE);
    }
    else
    {
        impl.gl.glDisable(GL_DEPTH_TEST);
        impl.gl.glDepthMask(GL_FALSE);
    }
}

void OpenGLRenderBackend::applyViewport()
{
    Impl& impl = *m_impl;
    if (impl.bound_target != nullptr)
        impl.gl.glViewport(0, 0, static_cast<GLsizei>(impl.bound_target->width()),
                           static_cast<GLsizei>(impl.bound_target->height()));
    else
        impl.gl.glViewport(0, 0, static_cast<GLsizei>(impl.config.width),
                           static_cast<GLsizei>(impl.config.height));
}

void OpenGLRenderBackend::draw(GpuMesh& mesh, ShaderProgram& program)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    applyViewport();
    program.use();
    mesh.drawIndexed();
    impl.drainGlErrors("draw");
}

void OpenGLRenderBackend::submit(const DrawSubmission& submission)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    submitImpl(submission);  // validates; throws std::invalid_argument when invalid
}

TextureView* OpenGLRenderBackend::renderTargetTexture(RenderTarget* target,
                                                      std::uint32_t attachment)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return nullptr;
    }
    opengl::GlRenderTarget* known = findGlTarget(impl.targets, target);
    if (known == nullptr || known->released || impl.bound_target == known)
    {
        ++impl.violations;
        return nullptr;
    }
    if (attachment == kDepthAttachment)
    {
        if (!known->hasDepthAttachment())
        {
            ++impl.violations;
            return nullptr;
        }
        return known->depthView();
    }
    if (attachment < known->colorAttachmentCount())
        return known->colorView(attachment);
    ++impl.violations;
    return nullptr;
}

void OpenGLRenderBackend::copyDepthAttachment(RenderTarget* source, RenderTarget* destination)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    opengl::GlRenderTarget* src = findGlTarget(impl.targets, source);
    opengl::GlRenderTarget* dst = findGlTarget(impl.targets, destination);
    if (src == nullptr || dst == nullptr || src->released || dst->released ||
        !src->hasDepthAttachment() || !dst->hasDepthAttachment() || source == destination ||
        src->width() != dst->width() || src->height() != dst->height() ||
        impl.bound_target == src || impl.bound_target == dst)
    {
        ++impl.violations;
        return;
    }
    impl.gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, src->framebufferId());
    impl.gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->framebufferId());
    impl.gl.glBlitFramebuffer(0, 0, static_cast<GLsizei>(src->width()),
                              static_cast<GLsizei>(src->height()), 0, 0,
                              static_cast<GLsizei>(dst->width()),
                              static_cast<GLsizei>(dst->height()), GL_DEPTH_BUFFER_BIT,
                              GL_NEAREST);
    // Restore the previous bindings: the current pass (if any) stays bound.
    impl.gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    impl.gl.glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER,
        impl.bound_target != nullptr ? impl.bound_target->framebufferId() : 0);
    impl.gl.glBindFramebuffer(GL_FRAMEBUFFER,
                              impl.bound_target != nullptr ? impl.bound_target->framebufferId() : 0);
    impl.drainGlErrors("copyDepthAttachment");
}

void OpenGLRenderBackend::bindTexture(const TextureView* view, std::uint32_t unit)
{
    Impl& impl = *m_impl;
    if (!impl.ready() || view == nullptr)
    {
        ++impl.violations;
        return;
    }
    const auto* glView = dynamic_cast<const opengl::GlTextureView*>(view);
    if (glView == nullptr)
    {
        ++impl.violations;
        return;
    }
    impl.gl.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    impl.gl.glBindTexture(GL_TEXTURE_2D, glView->textureId());
    impl.drainGlErrors("bindTexture");
}

void OpenGLRenderBackend::releaseTexture3D(TextureView* view)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    opengl::GlTexture3DView* known = findGlVolume(impl.volumes, view);
    if (known == nullptr)
    {
        ++impl.violations;
        return;
    }
    // Deleting the view destroys the GL texture through its destructor, the
    // rule the targets follow. The registry entry goes with it: a released
    // volume is not addressable, unlike a released target, because no layer
    // target may outlive it.
    for (auto it = impl.volumes.begin(); it != impl.volumes.end(); ++it)
    {
        if (it->get() == known)
        {
            impl.volumes.erase(it);
            break;
        }
    }
    impl.drainGlErrors("releaseTexture3D");
}

TextureView* OpenGLRenderBackend::createTexture3D(const Texture3DDesc& desc)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error("OpenGLRenderBackend::createTexture3D: not initialised");
    }
    validateTexture3DDesc(desc);  // throws, the same rejection the null backend makes
    auto volume = std::make_unique<opengl::GlTexture3DView>(impl.gl, desc);
    opengl::GlTexture3DView* raw = volume.get();
    impl.volumes.push_back(std::move(volume));
    impl.drainGlErrors("createTexture3D");
    return raw;
}

void OpenGLRenderBackend::bindTexture3D(const TextureView* view, std::uint32_t unit)
{
    Impl& impl = *m_impl;
    if (!impl.ready() || view == nullptr)
    {
        ++impl.violations;
        return;
    }
    const auto* glView = dynamic_cast<const opengl::GlTexture3DView*>(view);
    if (glView == nullptr)
    {
        ++impl.violations;  // a 2D attachment view is not a volume
        return;
    }
    impl.gl.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    impl.gl.glBindTexture(GL_TEXTURE_3D, glView->textureId());
    impl.drainGlErrors("bindTexture3D");
}

void OpenGLRenderBackend::fillTexture3D(TextureView* view, std::uint32_t layer, const void* data,
                                        std::size_t sizeBytes)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    opengl::GlTexture3DView* volume = findGlVolume(impl.volumes, view);
    if (volume == nullptr)
        throw std::invalid_argument("fillTexture3D: view is not a 3D texture of this backend");
    volume->uploadSlice(layer, data, sizeBytes);
    impl.drainGlErrors("fillTexture3D");
}

bool OpenGLRenderBackend::readTexture3D(const TextureView* view, std::uint32_t layer, void* data,
                                        std::size_t sizeBytes)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return false;
    }
    opengl::GlTexture3DView* volume = findGlVolume(impl.volumes, view);
    if (volume == nullptr || layer >= volume->depth() || sizeBytes != volume->sliceBytes())
    {
        ++impl.violations;
        return false;
    }
    volume->readSlice(layer, data, sizeBytes);
    impl.drainGlErrors("readTexture3D");
    return true;
}

RenderTarget* OpenGLRenderBackend::createVolumeLayerTarget(TextureView* volume,
                                                          std::uint32_t layer)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error("OpenGLRenderBackend::createVolumeLayerTarget: not initialised");
    }
    opengl::GlTexture3DView* known = findGlVolume(impl.volumes, volume);
    if (known == nullptr)
        throw std::invalid_argument(
            "createVolumeLayerTarget: view is not a 3D texture of this backend");
    VolumeLayerDesc desc;
    desc.volume.width = known->width();
    desc.volume.height = known->height();
    desc.volume.depth = known->depth();
    desc.volume.format = known->format();
    desc.layer = layer;
    // Throws on a bad layer, the same rule the null backend applies.
    auto target = std::make_unique<opengl::GlRenderTarget>(impl.gl, known->textureId(), desc);
    RenderTarget* raw = target.get();
    impl.targets.push_back(std::move(target));
    impl.drainGlErrors("createVolumeLayerTarget");
    return raw;
}

void OpenGLRenderBackend::ensureLightingProgram()
{
    Impl& impl = *m_impl;
    if (!impl.lighting_program)
        impl.lighting_program = std::shared_ptr<ShaderProgram>(
            createShaderProgram(ShaderProgramId::ForwardLighting));
}

void OpenGLRenderBackend::submitImpl(const DrawSubmission& submission)
{
    Impl& impl = *m_impl;
    submission.validate();  // enforces the per-object light limit and parameter ranges
    ensureLightingProgram();

    // Camera set through setCamera() (documented diagnostic default:
    // eye (0,0,1) looking at the origin, up +Y, fovY 90 degrees).
    const CameraParams& camera = impl.camera;
    const core::mat4 view = core::lookAt(camera.eye, camera.target, camera.up);
    const core::mat4 projection =
        core::perspective(camera.fovYRad, camera.aspect, camera.near, camera.far);
    const core::mat4 mvp = projection * view * submission.model;

    applyViewport();
    applyDepthState();

    ShaderProgram& program = *impl.lighting_program;
    program.use();
    program.setUniform("mvp", mvp);
    program.setUniform("model", submission.model);
    program.setUniform("viewMatrix", view);
    program.setUniform("cameraPos", camera.eye);

    const SurfaceParams& surface = submission.material.surfaceParams();
    program.setUniform("albedo", surface.albedo);
    program.setUniform("specular", surface.specular);
    program.setUniform("shininess", surface.shininess);
    program.setUniform("emissive", surface.emissive);
    program.setUniform("ambient", submission.ambient);

    if (submission.sun.has_value())
    {
        program.setUniform("hasSun", 1);
        program.setUniform("sunDirection", core::normalise(submission.sun->direction));
        program.setUniform("sunColor", submission.sun->color);
        program.setUniform("sunIntensity", submission.sun->intensity);
    }
    else
    {
        program.setUniform("hasSun", 0);
    }

    // Cascaded sun shadow map carried by the submission (milestone 4c). The
    // shadow factor applies to the sun term only; dynamic lights stay
    // unshadowed (documented). The backend treats it as plain data: depth
    // textures, light matrices and split depths uploaded as uniforms.
    const ShadowMap* shadow = submission.shadow;
    if (shadow != nullptr && shadow->cascadeCount > 0)
    {
        const int count = static_cast<int>(shadow->cascadeCount);
        program.setUniform("hasShadowMap", 1);
        program.setUniform("cascadeCount", count);
        program.setUniform("shadowBias", shadow->bias);
        program.setUniform("filterTolerance", shadow->filterTolerance);
        program.setUniform("shadowMapSize", static_cast<float>(shadow->mapSize));
        program.setUniform("debugCascades", shadow->debug ? 1 : 0);
        for (int i = 0; i < count; ++i)
        {
            TextureView* texView =
                renderTargetTexture(shadow->targets[static_cast<std::size_t>(i)]);
            if (texView == nullptr)
            {
                program.setUniform("hasShadowMap", 0);
                break;
            }
            auto* glView = static_cast<opengl::GlTextureView*>(texView);
            impl.gl.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            impl.gl.glBindTexture(GL_TEXTURE_2D, glView->textureId());
            const std::string index = "[" + std::to_string(i) + "]";
            program.setUniform(("shadowMaps" + index).c_str(), i);
            program.setUniform(("lightMatrices" + index).c_str(),
                               shadow->lightMatrices[static_cast<std::size_t>(i)]);
        }
        for (int i = 0; i <= count; ++i)
        {
            const std::string index = "[" + std::to_string(i) + "]";
            program.setUniform(("splitDepths" + index).c_str(),
                               shadow->splitDepths[static_cast<std::size_t>(i)]);
        }
    }
    else
    {
        program.setUniform("hasShadowMap", 0);
    }

    // Dual paraboloid point-light shadow maps (milestone 4d), plain data from
    // the submission. Texture units 4..7; the cascades use 0..3.
    const PointShadowMaps* pointShadows = submission.pointShadows;
    if (submission.shadowLightRequests > kMaxShadowCastingPointLights)
        ++impl.violations;  // documented: over-budget request, excess lights unshadowed
    if (pointShadows != nullptr && !pointShadows->entries.empty())
    {
        const int entryCount = static_cast<int>(pointShadows->entries.size());
        program.setUniform("pointShadowCount", entryCount);
        program.setUniform("pointFilterTolerance", pointShadows->filterTolerance);
        program.setUniform("pointShadowMapSize", static_cast<float>(pointShadows->mapSize));
        for (int k = 0; k < entryCount; ++k)
        {
            const PointShadowMapEntry& entry =
                pointShadows->entries[static_cast<std::size_t>(k)];
            const std::string index = "[" + std::to_string(k) + "]";
            TextureView* frontView = renderTargetTexture(entry.front);
            TextureView* backView = renderTargetTexture(entry.back);
            if (frontView == nullptr || backView == nullptr)
            {
                program.setUniform("pointShadowCount", 0);
                break;
            }
            auto* frontGl = static_cast<opengl::GlTextureView*>(frontView);
            auto* backGl = static_cast<opengl::GlTextureView*>(backView);
            impl.gl.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(4 + 2 * k));
            impl.gl.glBindTexture(GL_TEXTURE_2D, frontGl->textureId());
            impl.gl.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(5 + 2 * k));
            impl.gl.glBindTexture(GL_TEXTURE_2D, backGl->textureId());
            program.setUniform(("pointShadowFront" + index).c_str(), 4 + 2 * k);
            program.setUniform(("pointShadowBack" + index).c_str(), 5 + 2 * k);
            program.setUniform(("pointLightViews" + index).c_str(), entry.lightView);
            const PointLight& pointLight =
                std::get<PointLight>(submission.lights[entry.lightIndex]);
            program.setUniform(("pointLightPos" + index).c_str(), pointLight.position);
            program.setUniform(("pointLightRange" + index).c_str(),
                               core::vec2{entry.near, entry.far});
            // D4: paraboloid depth is linear in world distance, so a constant
            // bias is sufficient — the world bias divided by the depth range.
            const float bias01 = pointShadows->biasWorld / (entry.far - entry.near);
            program.setUniform(("pointShadowBias" + index).c_str(), bias01);
        }
        // Association (documented): dynamic light i uses entry k whose
        // lightIndex == i; otherwise slot -1 = no shadow map, full light.
        for (int i = 0; i < static_cast<int>(submission.lights.size()); ++i)
        {
            int slot = -1;
            for (int k = 0; k < entryCount; ++k)
                if (pointShadows->entries[static_cast<std::size_t>(k)].lightIndex ==
                    static_cast<std::uint32_t>(i))
                    slot = k;
            program.setUniform(("pointLightSlot[" + std::to_string(i) + "]").c_str(), slot);
        }
    }
    else
    {
        program.setUniform("pointShadowCount", 0);
    }

    // Light volume compose (milestone 7, fourth increment): the volume, sampled
    // at the fragment's world position. Texture unit 9 (the first free unit:
    // the cascades use 0..3, the point shadows 4..7). hasVolume = 0 keeps the
    // frame unchanged (the compose flag is off by default). The sampler unit
    // is set in BOTH branches: the GLSL default is unit 0, and a sampler3D
    // pointing at a unit that already carries a sampler2D makes the draw
    // invalid even when hasVolume = 0 keeps the sampling code path out.
    if (submission.volume != nullptr && submission.volume->texture != nullptr)
    {
        program.setUniform("hasVolume", 1);
        bindTexture3D(submission.volume->texture, 9u);
        program.setUniform("volumeTex", 9);
        program.setUniform("volumeOrigin", submission.volume->origin);
        program.setUniform("volumeExtent", submission.volume->extent);
    }
    else
    {
        program.setUniform("hasVolume", 0);
        program.setUniform("volumeTex", 9);
    }

    const int light_count = static_cast<int>(submission.lights.size());
    program.setUniform("lightCount", light_count);
    for (int i = 0; i < light_count; ++i)
    {
        const std::string index = "[" + std::to_string(i) + "]";
        std::visit(
            [&](const auto& light) {
                using T = std::decay_t<decltype(light)>;
                if constexpr (std::is_same_v<T, PointLight>)
                {
                    program.setUniform(("lightType" + index).c_str(), 0);
                    program.setUniform(("lightPos" + index).c_str(), light.position);
                    program.setUniform(("lightDir" + index).c_str(), core::vec3{0.0f, 0.0f, 0.0f});
                    program.setUniform(("lightColor" + index).c_str(), light.color);
                    program.setUniform(("lightIntensity" + index).c_str(), light.intensity);
                    program.setUniform(("lightRange" + index).c_str(), light.radius);
                    program.setUniform(("lightInner" + index).c_str(), 0.0f);
                    program.setUniform(("lightOuter" + index).c_str(), 0.0f);
                }
                else
                {
                    program.setUniform(("lightType" + index).c_str(), 1);
                    program.setUniform(("lightPos" + index).c_str(), light.position);
                    program.setUniform(("lightDir" + index).c_str(),
                                       core::normalise(light.direction));
                    program.setUniform(("lightColor" + index).c_str(), light.color);
                    program.setUniform(("lightIntensity" + index).c_str(), light.intensity);
                    program.setUniform(("lightRange" + index).c_str(), light.range);
                    program.setUniform(("lightInner" + index).c_str(), light.innerAngle);
                    program.setUniform(("lightOuter" + index).c_str(), light.outerAngle);
                }
            },
            submission.lights[static_cast<std::size_t>(i)]);
    }

    submission.mesh->drawIndexed();
    impl.drainGlErrors("submit");
}

RenderTarget* OpenGLRenderBackend::createRenderTarget(const RenderTargetDesc& desc)
{
    validateRenderTargetDesc(desc);  // throws std::invalid_argument when invalid/incomplete
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error("OpenGLRenderBackend::createRenderTarget: not initialised");
    }
    auto target =
        std::make_unique<opengl::GlRenderTarget>(impl.gl, desc);  // throws when the FBO is incomplete
    RenderTarget* raw = target.get();
    impl.targets.push_back(std::move(target));
    return raw;
}

void OpenGLRenderBackend::setDepthTestEnabled(bool enabled)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    impl.depth_test = enabled;
    if (enabled)
    {
        impl.gl.glEnable(GL_DEPTH_TEST);
        impl.gl.glDepthFunc(GL_LESS);
        impl.gl.glDepthMask(GL_TRUE);
    }
    else
    {
        // No test and no writes (decision 20): fullscreen passes must not
        // touch the depth attachment of the target they render into.
        impl.gl.glDisable(GL_DEPTH_TEST);
        impl.gl.glDepthMask(GL_FALSE);
    }
    impl.drainGlErrors("setDepthTestEnabled");
}

void OpenGLRenderBackend::beginRenderPass(RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    if (target == nullptr)
    {
        // Target the default framebuffer.
        impl.bound_target = nullptr;
        impl.gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        applyViewport();
        applyDepthState();
        impl.drainGlErrors("beginRenderPass");
        return;
    }
    opengl::GlRenderTarget* known = findGlTarget(impl.targets, target);
    if (known == nullptr || known->released)
    {
        // Unknown or released handle: violation, never dereferenced.
        ++impl.violations;
        return;
    }
    // Beginning a pass while another target is bound simply replaces it.
    impl.bound_target = known;
    impl.gl.glBindFramebuffer(GL_FRAMEBUFFER, known->framebufferId());
    applyViewport();
    // Depth state for the pass follows the device flag (decision 20): a
    // shadow/depth pass writes depth; a fullscreen post pass does not.
    applyDepthState();
    impl.drainGlErrors("beginRenderPass");
}

void OpenGLRenderBackend::endRenderPass()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    impl.bound_target = nullptr;
    impl.gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    applyViewport();
    impl.drainGlErrors("endRenderPass");
}

void OpenGLRenderBackend::setCamera(const CameraParams& camera)
{
    camera.validate();  // throws std::invalid_argument on invalid parameters
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error("OpenGLRenderBackend::setCamera: not initialised");
    }
    impl.camera = camera;
}

CameraParams OpenGLRenderBackend::camera() const
{
    return m_impl->camera;
}

void OpenGLRenderBackend::releaseRenderTarget(RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    opengl::GlRenderTarget* known = findGlTarget(impl.targets, target);
    if (known == nullptr || known->released)
    {
        ++impl.violations;
        return;
    }
    if (impl.bound_target == known)
    {
        impl.bound_target = nullptr;
        impl.gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        applyViewport();
    }
    known->release();  // deletes GL objects; the handle stays in the registry
}

TextureView* OpenGLRenderBackend::renderTargetTexture(RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return nullptr;
    }
    opengl::GlRenderTarget* known = findGlTarget(impl.targets, target);
    if (known == nullptr || known->released || impl.bound_target == known)
    {
        // Unknown, released, or bound for rendering (feedback loop).
        ++impl.violations;
        return nullptr;
    }
    if (known->hasColorAttachment())
        return known->colorView(0);
    if (known->hasDepthAttachment())
        return known->depthView();
    ++impl.violations;
    return nullptr;
}



void OpenGLRenderBackend::ensureTriangle()
{
    Impl& impl = *m_impl;
    if (impl.triangle_ready)
        return;

    // Triangle in local space; drawTriangle maps it to clip space through an
    // mvp matrix composed with the core math module.
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 3 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    mesh.setLayout(layout);
    mesh.setVertexData(std::vector<float>{-0.5f, -0.5f, 0.0f,  //
                                          0.5f, -0.5f, 0.0f,   //
                                          0.0f, 0.5f, 0.0f});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2});

    impl.tri_mesh = createGpuMesh();
    impl.tri_mesh->upload(mesh);
    impl.tri_shader = std::shared_ptr<ShaderProgram>(
        createShaderProgram(ShaderProgramId::FlatUnlit));
    impl.tri_material = std::make_shared<Material>(impl.tri_shader);

    impl.drainGlErrors("ensureTriangle");
    impl.triangle_ready = true;
}

void OpenGLRenderBackend::drawTriangle()
{
    Impl& impl = *m_impl;
    ensureTriangle();

    // Full-frame viewport: NDC maps onto the whole framebuffer/target, so
    // the result is correct for non-square sizes.
    applyViewport();

    const float aspect = static_cast<float>(impl.config.width) /
                         static_cast<float>(impl.config.height);
    const core::mat4 model = core::Transform{}.compose();
    const core::mat4 view =
        core::lookAt(core::vec3{0.0f, 0.0f, 1.0f}, core::vec3{0.0f, 0.0f, 0.0f},
                     core::vec3{0.0f, 1.0f, 0.0f});
    const core::mat4 projection = core::perspective(kPi / 2.0f, aspect, 0.1f, 100.0f);
    const core::mat4 mvp = projection * view * model;

    const ClearColor color = impl.config.triangle_color;
    impl.tri_material->setParameter("mvp", mvp);
    impl.tri_material->setParameter(
        "color", core::vec4{static_cast<float>(color.r) / 255.0f,
                            static_cast<float>(color.g) / 255.0f,
                            static_cast<float>(color.b) / 255.0f,
                            static_cast<float>(color.a) / 255.0f});
    impl.tri_material->apply();
    impl.tri_mesh->drawIndexed();
    impl.drainGlErrors("drawTriangle");
}

}  // namespace grace::render
