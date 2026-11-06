#include "grace/render/opengl_render_backend.hpp"

#include <SDL2/SDL.h>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "grace/render/opengl/gl_loader.hpp"

namespace grace::render {

namespace {

constexpr int kRequestedMajor = 3;
constexpr int kRequestedMinor = 3;

constexpr const char* kVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShaderSource = R"(#version 330 core
uniform vec4 color;
out vec4 fragment;
void main()
{
    fragment = color;
}
)";

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

    bool has_center = false;
    ClearColor center_pixel{0, 0, 0, 0};
    float center_depth = 0.0f;
    std::int32_t center_stencil = 0;

    std::vector<std::pair<std::uint32_t, std::uint32_t>> readback_queue;
    std::vector<ReadbackSample> last_readback;

    std::uint64_t violations = 0;
    std::uint64_t gl_errors = 0;
    std::string last_error;

    // Diagnostic triangle resources.
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint program = 0;
    bool triangle_ready = false;

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

    // Readback happens before the swap: the back buffer contents are
    // undefined after SDL_GL_SwapWindow.
    if (impl.config.readback_center)
        readCenter();
    captureQueuedReadbacks();

    SDL_GL_SwapWindow(impl.window);
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

    if (impl.triangle_ready)
    {
        impl.gl.glDeleteProgram(impl.program);
        impl.gl.glDeleteBuffers(1, &impl.vbo);
        impl.gl.glDeleteVertexArrays(1, &impl.vao);
        impl.drainGlErrors("shutdown");
        impl.triangle_ready = false;
        impl.program = 0;
        impl.vbo = 0;
        impl.vao = 0;
    }
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

ClearColor OpenGLRenderBackend::readPixel(std::uint32_t x, std::uint32_t y)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return ClearColor{0, 0, 0, 0};
    }
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

unsigned int OpenGLRenderBackend::compileShader(bool vertex, const char* source)
{
    Impl& impl = *m_impl;
    const GLuint shader =
        impl.gl.glCreateShader(vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    impl.gl.glShaderSource(shader, 1, &source, nullptr);
    impl.gl.glCompileShader(shader);
    GLint status = GL_FALSE;
    impl.gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        impl.gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        impl.gl.glGetShaderInfoLog(shader, length, nullptr, log.data());
        impl.gl.glDeleteShader(shader);
        throw std::runtime_error(std::string("OpenGL ") +
                                 (vertex ? "vertex" : "fragment") +
                                 " shader compile failed: " + log);
    }
    return shader;
}

unsigned int OpenGLRenderBackend::linkProgram(unsigned int vertexShader,
                                              unsigned int fragmentShader)
{
    Impl& impl = *m_impl;
    const GLuint program = impl.gl.glCreateProgram();
    impl.gl.glAttachShader(program, vertexShader);
    impl.gl.glAttachShader(program, fragmentShader);
    impl.gl.glLinkProgram(program);
    impl.gl.glDeleteShader(vertexShader);
    impl.gl.glDeleteShader(fragmentShader);
    GLint status = GL_FALSE;
    impl.gl.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        impl.gl.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        impl.gl.glGetProgramInfoLog(program, length, nullptr, log.data());
        impl.gl.glDeleteProgram(program);
        throw std::runtime_error("OpenGL program link failed: " + log);
    }
    return program;
}

void OpenGLRenderBackend::ensureTriangle()
{
    Impl& impl = *m_impl;
    if (impl.triangle_ready)
        return;

    // NDC triangle covering the centre of the framebuffer.
    static const GLfloat kVertices[6] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f};

    impl.gl.glGenVertexArrays(1, &impl.vao);
    impl.gl.glBindVertexArray(impl.vao);
    impl.gl.glGenBuffers(1, &impl.vbo);
    impl.gl.glBindBuffer(GL_ARRAY_BUFFER, impl.vbo);
    impl.gl.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kVertices)),
                         kVertices, GL_STATIC_DRAW);
    impl.gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    impl.gl.glEnableVertexAttribArray(0);

    const GLuint vertex_shader = compileShader(true, kVertexShaderSource);
    const GLuint fragment_shader = compileShader(false, kFragmentShaderSource);
    impl.program = linkProgram(vertex_shader, fragment_shader);

    impl.drainGlErrors("ensureTriangle");
    impl.triangle_ready = true;
}

void OpenGLRenderBackend::drawTriangle()
{
    Impl& impl = *m_impl;
    ensureTriangle();

    // Full-window viewport: NDC maps onto the whole framebuffer, so the
    // result is correct for non-square window sizes.
    impl.gl.glViewport(0, 0, static_cast<GLsizei>(impl.config.width),
                       static_cast<GLsizei>(impl.config.height));
    impl.gl.glUseProgram(impl.program);
    const ClearColor color = impl.config.triangle_color;
    const GLint location = impl.gl.glGetUniformLocation(impl.program, "color");
    if (location >= 0)
    {
        impl.gl.glUniform4f(location, static_cast<GLfloat>(color.r) / 255.0f,
                            static_cast<GLfloat>(color.g) / 255.0f,
                            static_cast<GLfloat>(color.b) / 255.0f,
                            static_cast<GLfloat>(color.a) / 255.0f);
    }
    impl.gl.glBindVertexArray(impl.vao);
    impl.gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    impl.drainGlErrors("drawTriangle");
}

}  // namespace grace::render
