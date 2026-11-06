#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "grace/render/render_backend.hpp"
#include "test_support.hpp"

namespace grace::test {

namespace detail {

// Detection idiom: backends that expose an event log (the null backend) get
// their recorded call sequence asserted as well.
template <typename B, typename = void>
struct has_event_log : std::false_type
{
};
template <typename B>
struct has_event_log<B, std::void_t<decltype(std::declval<const B&>().events())>>
    : std::true_type
{
};

inline bool withinOne(grace::render::ClearColor actual, grace::render::ClearColor expected)
{
    const auto diff = [](std::uint8_t a, std::uint8_t b) {
        return a > b ? static_cast<int>(a) - static_cast<int>(b)
                     : static_cast<int>(b) - static_cast<int>(a);
    };
    return diff(actual.r, expected.r) <= 1 && diff(actual.g, expected.g) <= 1 &&
           diff(actual.b, expected.b) <= 1 && diff(actual.a, expected.a) <= 1;
}

inline grace::render::Mesh makeContractTriangle()
{
    using namespace grace::render;
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
    return mesh;
}

constexpr const char* kContractVertexShader =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "void main() { gl_Position = vec4(position, 1.0); }\n";

constexpr const char* kContractFragmentShader =
    "#version 330 core\n"
    "out vec4 fragment;\n"
    "void main() { fragment = vec4(1.0); }\n";

}  // namespace detail

/// Shared RenderBackend contract checks, run unchanged against every backend
/// implementation (null, OpenGL). Covers the full contract: clear lifecycle,
/// mesh/shader/draw, readback with its documented per-backend value, and
/// protocol violations after shutdown (draw, readback and clear must each be
/// counted).
///
/// This header is platform-neutral: it includes no OpenGL or SDL headers, so
/// the null backend stays free of any graphics dependency.
template <typename Backend>
void runContractChecks(Backend& backend, grace::render::ClearColor color,
                       std::uint32_t frames)
{
    GRACE_CHECK(backend.width() > 0u);
    GRACE_CHECK(backend.height() > 0u);
    GRACE_CHECK_EQ(backend.violationCount(), 0u);

    backend.setClearColor(color);
    GRACE_CHECK(backend.clearColor() == color);

    for (std::uint32_t frame = 0; frame < frames; ++frame)
        backend.clear();

    GRACE_CHECK_EQ(backend.violationCount(), 0u);

    // Readback returns the documented value in every backend: the value
    // clear() writes (null: exactly the clear colour; OpenGL: the framebuffer
    // contents, within one unit per channel). The queued pixel is captured
    // with the next clear, before any buffer swap on real backends.
    backend.queueReadback(1, 1);
    backend.clear();
    GRACE_CHECK(backend.hasCenterPixel());
    GRACE_CHECK(detail::withinOne(backend.centerPixel(), color));
    GRACE_CHECK_EQ(backend.lastReadback().size(), 1u);
    GRACE_CHECK(detail::withinOne(backend.lastReadback()[0].color, color));

    // Mesh + shader + draw are part of the shared contract.
    auto program = backend.createShaderProgram(detail::kContractVertexShader,
                                               detail::kContractFragmentShader);
    GRACE_CHECK(program != nullptr);
    auto mesh = backend.createGpuMesh();
    GRACE_CHECK(mesh != nullptr);
    mesh->upload(detail::makeContractTriangle());
    backend.draw(*mesh, *program);
    GRACE_CHECK_EQ(backend.violationCount(), 0u);

    // A draw call is recorded (event-log backends such as the null backend).
    if constexpr (detail::has_event_log<Backend>::value)
    {
        bool draw_recorded = false;
        for (const auto& event : backend.events())
            if (event.find("draw") != std::string::npos)
                draw_recorded = true;
        GRACE_CHECK(draw_recorded);
    }

    // A lit submission is part of the shared contract too.
    grace::render::DrawSubmission submission;
    submission.mesh = mesh.get();
    submission.material.setSurfaceParams(grace::render::SurfaceParams{});
    submission.lights.push_back(grace::render::PointLight{});
    backend.submit(submission);
    GRACE_CHECK_EQ(backend.violationCount(), 0u);

    backend.shutdown();

    // Post-shutdown misuse: draw, readback, clear and submit are each a
    // violation.
    backend.draw(*mesh, *program);
    backend.readback();
    backend.clear();
    backend.submit(submission);
    GRACE_CHECK_EQ(backend.violationCount(), 4u);

    GRACE_CHECK(!backend.describe().empty());
}

}  // namespace grace::test
