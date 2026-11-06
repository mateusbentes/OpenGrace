#include "grace/render/mesh.hpp"
#include "grace/render/opengl_render_backend.hpp"
#include "grace/render/shader.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::render::ClearColor;
using grace::render::ComponentType;
using grace::render::Mesh;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;
using grace::render::VertexAttribute;
using grace::render::VertexLayout;
using grace::render::VertexSemantic;

constexpr const char* kFlatVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
void main()
{
    gl_Position = vec4(position, 1.0);
}
)";

constexpr const char* kFlatFragmentShader = R"(#version 330 core
uniform vec4 color;
out vec4 fragment;
void main()
{
    fragment = color;
}
)";

bool withinOne(ClearColor actual, ClearColor expected)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= 1 && diff(actual.g, expected.g) <= 1 &&
           diff(actual.b, expected.b) <= 1 && diff(actual.a, expected.a) <= 1;
}

Mesh makeTriangle()
{
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 3 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    mesh.setLayout(layout);
    mesh.setVertexData(std::vector<float>{-0.5f, -0.5f, 0.0f,  //
                                           0.5f, -0.5f, 0.0f,  //
                                           0.0f, 0.5f, 0.0f});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2});
    return mesh;
}

template <typename F>
std::string catchExceptionMessage(F&& fn)
{
    try
    {
        fn();
    }
    catch (const std::exception& error)
    {
        return std::string(error.what());
    }
    return std::string();
}

}  // namespace

int main()
{
    OpenGLBackendConfig config;
    config.width = 128;
    config.height = 96;
    config.hidden = true;
    config.readback_center = true;

    std::unique_ptr<OpenGLRenderBackend> backend;
    try
    {
        backend = std::make_unique<OpenGLRenderBackend>(config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        std::printf("SKIP: no GL context in this environment: %s\n", error.what());
        return 77;
    }

    // A valid pair compiles and links; uniform locations are cached.
    auto program = backend->createShaderProgram(kFlatVertexShader, kFlatFragmentShader);
    GRACE_CHECK(program != nullptr);
    const std::int32_t color_loc = program->uniformLocation("color");
    GRACE_CHECK(color_loc >= 0);
    GRACE_CHECK_EQ(program->uniformLocation("color"), color_loc);  // cached, same value
    GRACE_CHECK_EQ(program->uniformLocation("does_not_exist"), -1);

    // Empty sources are rejected before reaching the driver.
    {
        const std::string message = catchExceptionMessage(
            [&] { static_cast<void>(backend->createShaderProgram("", kFlatFragmentShader)); });
        GRACE_CHECK(message.find("vertex source is empty") != std::string::npos);
    }

    // A deliberately broken vertex shader fails with its info log in the message.
    {
        const std::string broken_vertex = R"(#version 330 core
layout(location = 0) in vec3 position;
void main()
{
    gl_Position = vec4(broken_identifier_xyz, 1.0);
}
)";
        const std::string message = catchExceptionMessage(
            [&] { static_cast<void>(backend->createShaderProgram(broken_vertex, kFlatFragmentShader)); });
        std::printf("broken vertex shader message: %s\n", message.c_str());
        GRACE_CHECK(message.find("compile failed") != std::string::npos);
        GRACE_CHECK(message.find("info log:") != std::string::npos);
        // The info log text itself must be present, not a bare status code.
        GRACE_CHECK(message.size() > 80u);
    }

    // A deliberately broken fragment shader fails the same way.
    {
        const std::string broken_fragment = R"(#version 330 core
out vec4 fragment;
void main()
{
    fragment = color_uniform_that_does_not_exist;
}
)";
        const std::string message = catchExceptionMessage(
            [&] { static_cast<void>(backend->createShaderProgram(kFlatVertexShader, broken_fragment)); });
        std::printf("broken fragment shader message: %s\n", message.c_str());
        GRACE_CHECK(message.find("compile failed") != std::string::npos);
        GRACE_CHECK(message.find("info log:") != std::string::npos);
        GRACE_CHECK(message.size() > 80u);
    }

    // A uniform set through the cached path reaches the shader and changes the
    // rendered result.
    {
        auto gpu_mesh = backend->createGpuMesh();
        gpu_mesh->upload(makeTriangle());

        const ClearColor clear_color{0, 0, 0, 255};
        backend->setClearColor(clear_color);

        const grace::core::vec4 red{200.0f / 255.0f, 0.0f, 0.0f, 1.0f};
        program->use();
        program->setUniform("color", red);
        backend->clear();
        backend->draw(*gpu_mesh, *program);
        backend->readback();
        const ClearColor expected_red{200, 0, 0, 255};
        GRACE_CHECK(withinOne(backend->centerPixel(), expected_red));

        const grace::core::vec4 green{0.0f, 200.0f / 255.0f, 0.0f, 1.0f};
        program->use();
        program->setUniform("color", green);
        backend->clear();
        backend->draw(*gpu_mesh, *program);
        backend->readback();
        const ClearColor expected_green{0, 200, 0, 255};
        GRACE_CHECK(withinOne(backend->centerPixel(), expected_green));

        // Sampler binding and scalar setters go through the same cached path.
        program->use();
        program->setUniform("color", 0.5f);
        program->setUniform("missing_uniform", 1);
        program->setUniformSampler("missing_sampler", 0);
        GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    }

    GRACE_CHECK_EQ(backend->violationCount(), 0u);
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("shader");
}
