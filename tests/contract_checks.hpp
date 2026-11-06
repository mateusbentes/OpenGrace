#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "grace/render/render_backend.hpp"
#include "grace/render/shadow.hpp"
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
    auto program = backend.createShaderProgram(grace::render::ShaderProgramId::FlatUnlit);
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

    // Render target lifecycle: a depth-only target (what a shadow pass
    // needs), bind, clear into it, texture view, release.
    grace::render::RenderTargetDesc target_desc;
    target_desc.width = 64;
    target_desc.height = 64;
    target_desc.colorAttachmentCount = 0;
    target_desc.depthAttachment = true;
    target_desc.depthFormat = grace::render::PixelFormat::Depth24Stencil8;
    grace::render::RenderTarget* target = backend.createRenderTarget(target_desc);
    GRACE_CHECK(target != nullptr);
    GRACE_CHECK(!target->hasColorAttachment());
    GRACE_CHECK(target->hasDepthAttachment());
    GRACE_CHECK_EQ(target->width(), 64u);

    // An incomplete target (no attachments) is rejected at creation.
    {
        grace::render::RenderTargetDesc bad;
        bad.width = 64;
        bad.height = 64;
        bad.colorAttachmentCount = 0;
        bad.depthAttachment = false;
        bool threw = false;
        try
        {
            static_cast<void>(backend.createRenderTarget(bad));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
    }

    // Camera round-trip and validation.
    {
        grace::render::CameraParams cam = backend.camera();
        GRACE_CHECK(cam.near > 0.0f);
        backend.setCamera(cam);
        GRACE_CHECK(backend.camera().far == cam.far);
        grace::render::CameraParams bad = cam;
        bad.far = bad.near;  // far <= near
        bool threw = false;
        try
        {
            backend.setCamera(bad);
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
    }

    // MRT descriptor (milestone 5, decision 9): two colour attachments + depth,
    // per-attachment views, cap enforced at creation.
    {
        grace::render::RenderTargetDesc mrt;
        mrt.width = 64;
        mrt.height = 64;
        mrt.colorAttachmentCount = 2;
        mrt.depthAttachment = true;
        grace::render::RenderTarget* mrtTarget = backend.createRenderTarget(mrt);
        GRACE_CHECK_EQ(mrtTarget->colorAttachmentCount(), 2u);
        GRACE_CHECK(backend.renderTargetTexture(mrtTarget, 0) != nullptr);
        GRACE_CHECK(!backend.renderTargetTexture(mrtTarget, 0)->isDepth());
        GRACE_CHECK(backend.renderTargetTexture(mrtTarget, 1) != nullptr);
        GRACE_CHECK(backend.renderTargetTexture(mrtTarget, grace::render::kDepthAttachment) != nullptr);
        GRACE_CHECK(backend.renderTargetTexture(mrtTarget, grace::render::kDepthAttachment)->isDepth());
        GRACE_CHECK(backend.renderTargetTexture(mrtTarget, 2) == nullptr);  // violation
        grace::render::RenderTargetDesc over;
        over.width = 8;
        over.height = 8;
        over.colorAttachmentCount = grace::render::kMaxColorAttachments + 1;
        bool threw = false;
        try
        {
            static_cast<void>(backend.createRenderTarget(over));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        GRACE_CHECK(threw);
        backend.releaseRenderTarget(mrtTarget);
    }

    // Render-pass composition: begin, clear, draw into the target, end.
    const std::uint64_t baseViolations = backend.violationCount();
    backend.beginRenderPass(target);
    backend.clear();
    backend.draw(*mesh, *program);
    // Texture view inside a render pass is a feedback loop: violation.
    GRACE_CHECK(backend.renderTargetTexture(target) == nullptr);
    GRACE_CHECK_EQ(backend.violationCount(), baseViolations + 1u);
    backend.endRenderPass();
    grace::render::TextureView* view = backend.renderTargetTexture(target);
    GRACE_CHECK(view != nullptr);
    GRACE_CHECK(view->isDepth());
    GRACE_CHECK_EQ(view->width(), 64u);
    backend.releaseRenderTarget(target);

    // Misuse: pass begin after release, texture after release, never-created
    // target.
    backend.beginRenderPass(target);
    GRACE_CHECK(backend.renderTargetTexture(target) == nullptr);
    grace::render::RenderTarget* bogus = reinterpret_cast<grace::render::RenderTarget*>(
        static_cast<std::uintptr_t>(0x1));
    backend.beginRenderPass(bogus);
    GRACE_CHECK_EQ(backend.violationCount(), baseViolations + 4u);

    // Point-light (paraboloid) shadow maps, assembled from the same pass
    // primitives — one pair of depth targets for one point light.
    {
        grace::render::PointShadowConfig pointConfig;
        pointConfig.mapSize = 64;
        pointConfig.far = 10.0f;
        grace::render::DynamicLight pointLight = grace::render::PointLight{};
        grace::render::PointShadowMaps pointMaps = grace::render::renderPointShadowMaps(
            backend, {pointLight}, {submission}, pointConfig);
        GRACE_CHECK_EQ(pointMaps.entries.size(), 1u);
        GRACE_CHECK(pointMaps.entries[0].front != nullptr);
        GRACE_CHECK(pointMaps.entries[0].back != nullptr);
        GRACE_CHECK_EQ(backend.violationCount(), baseViolations + 4u);  // no new violations

        grace::render::DrawSubmission lit = submission;
        lit.lights.clear();
        lit.lights.push_back(pointLight);
        lit.pointShadows = &pointMaps;
        lit.shadowLightRequests = 1;
        backend.submit(lit);
        GRACE_CHECK_EQ(backend.violationCount(), baseViolations + 4u);

        // Over-budget request: handled as documented (excess lights unshadowed)
        // and counted as a violation, never silently truncated.
        grace::render::DrawSubmission overBudget = lit;
        overBudget.shadowLightRequests = grace::render::kMaxShadowCastingPointLights + 1u;
        backend.submit(overBudget);
        GRACE_CHECK_EQ(backend.violationCount(), baseViolations + 5u);

        for (const grace::render::PointShadowMapEntry& entry : pointMaps.entries)
        {
            backend.releaseRenderTarget(entry.front);
            backend.releaseRenderTarget(entry.back);
        }
    }

    backend.shutdown();

    // Post-shutdown misuse: draw, readback, clear and submit are each a
    // violation.
    backend.draw(*mesh, *program);
    backend.readback();
    backend.clear();
    backend.submit(submission);
    GRACE_CHECK_EQ(backend.violationCount(), baseViolations + 9u);

    GRACE_CHECK(!backend.describe().empty());
}

}  // namespace grace::test
