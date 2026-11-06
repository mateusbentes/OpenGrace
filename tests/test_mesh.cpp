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

bool withinOne(ClearColor actual, ClearColor expected)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= 1 && diff(actual.g, expected.g) <= 1 &&
           diff(actual.b, expected.b) <= 1 && diff(actual.a, expected.a) <= 1;
}

Mesh makePositionMesh(const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices)
{
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 3 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0, false});
    mesh.setLayout(layout);
    mesh.setVertexData(vertices);
    mesh.setIndices(indices);
    return mesh;
}

template <typename F>
bool throwsWithMessage(F&& fn, const std::string& needle)
{
    try
    {
        fn();
    }
    catch (const std::invalid_argument& error)
    {
        return std::string(error.what()).find(needle) != std::string::npos;
    }
    return false;
}

}  // namespace

int main()
{
    OpenGLBackendConfig config;
    config.width = 128;
    config.height = 96;  // non-square on purpose
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

    auto program = backend->createShaderProgram(grace::render::ShaderProgramId::FlatUnlit);
    GRACE_CHECK(program != nullptr);

    const ClearColor clear_color{0, 0, 0, 255};
    backend->setClearColor(clear_color);

    // An indexed mesh produces the expected drawn pixel at the centre.
    {
        Mesh triangle = makePositionMesh(
            {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f}, {0, 1, 2});
        auto gpu_mesh = backend->createGpuMesh();
        gpu_mesh->upload(triangle);

        const grace::core::vec4 draw_color{200.0f / 255.0f, 60.0f / 255.0f, 40.0f / 255.0f, 1.0f};
        program->use();
        program->setUniform("color", draw_color);

        backend->clear();
        backend->draw(*gpu_mesh, *program);
        backend->queueReadback(config.width / 2u, config.height / 2u);
        backend->readback();

        const ClearColor expected{200, 60, 40, 255};
        GRACE_CHECK(backend->hasCenterPixel());
        GRACE_CHECK(withinOne(backend->centerPixel(), expected));
        GRACE_CHECK(!backend->lastReadback().empty());
        GRACE_CHECK(withinOne(backend->lastReadback()[0].color, expected));
    }

    // Invalid attribute layouts are rejected with a clear message.
    {
        Mesh bad_stride;
        bad_stride.setLayout(VertexLayout{0, {VertexAttribute{VertexSemantic::Position,
                                                              ComponentType::Float32, 3, 0, false}}});
        bad_stride.setVertexData(std::vector<float>{0.0f, 0.0f, 0.0f});
        bad_stride.setIndices(std::vector<std::uint32_t>{0, 0, 0});
        GRACE_CHECK(throwsWithMessage([&] { bad_stride.validate(); }, "stride"));

        Mesh bad_offset = makePositionMesh({0.0f, 0.0f, 0.0f}, {0, 0, 0});
        bad_offset.setLayout(VertexLayout{12, {VertexAttribute{VertexSemantic::Position,
                                                                ComponentType::Float32, 3, 100, false}}});
        GRACE_CHECK(throwsWithMessage([&] { bad_offset.validate(); }, "exceeds"));

        Mesh bad_index = makePositionMesh(
            {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0, 1, 7});
        GRACE_CHECK(throwsWithMessage([&] { bad_index.validate(); }, "out of vertex range"));

        // Upload validates too: the backend rejects the same invalid mesh.
        auto gpu_mesh = backend->createGpuMesh();
        GRACE_CHECK(throwsWithMessage([&] { gpu_mesh->upload(bad_index); }, "out of vertex range"));
    }

    // Re-upload replaces all previously uploaded data.
    {
        auto gpu_mesh = backend->createGpuMesh();

        Mesh covering = makePositionMesh(
            {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f}, {0, 1, 2});
        gpu_mesh->upload(covering);
        const grace::core::vec4 red{200.0f / 255.0f, 0.0f, 0.0f, 1.0f};
        program->use();
        program->setUniform("color", red);
        backend->clear();
        backend->draw(*gpu_mesh, *program);
        backend->readback();
        const ClearColor expected_red{200, 0, 0, 255};
        GRACE_CHECK(withinOne(backend->centerPixel(), expected_red));

        // Re-upload a tiny triangle in the top-right corner: the centre must
        // now show the clear colour again.
        Mesh corner = makePositionMesh(
            {0.8f, 0.8f, 0.0f, 1.0f, 0.8f, 0.0f, 0.8f, 1.0f, 0.0f}, {0, 1, 2});
        gpu_mesh->upload(corner);
        backend->clear();
        backend->draw(*gpu_mesh, *program);
        backend->readback();
        GRACE_CHECK(withinOne(backend->centerPixel(), clear_color));
    }

    GRACE_CHECK_EQ(backend->violationCount(), 0u);
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    backend->shutdown();

    return grace::test::summary("mesh");
}
