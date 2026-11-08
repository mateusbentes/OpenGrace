#include "grace/render/camera.hpp"
#include "grace/render/null_render_backend.hpp"
#include "grace/render/shadow.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::vec3;
using grace::render::CascadeConfig;
using grace::render::CameraParams;
using grace::render::computeCascadeSplits;
using grace::render::DirectionalLight;
using grace::render::DrawSubmission;
using grace::render::kMaxCascadeCount;
using grace::render::NullRenderBackend;
using grace::render::cascadeSliceCorners;
using grace::render::fitCascadeLightMatrix;
using grace::render::renderShadowCascades;
using grace::render::validateCascadeConfig;

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

template <typename F>
bool throwsInvalid(F&& fn)
{
    try
    {
        fn();
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    // The documented cascade count choice and its cap.
    GRACE_CHECK_EQ(kMaxCascadeCount, 4u);

    // Cascade splits: monotonic, covering [near, far], derived from the
    // camera parameters.
    {
        const std::vector<float> splits = computeCascadeSplits(0.1f, 100.0f, 4, 0.5f);
        GRACE_CHECK_EQ(splits.size(), 5u);
        GRACE_CHECK(near(splits.front(), 0.1f, 1e-6f));
        GRACE_CHECK(near(splits.back(), 100.0f, 1e-4f));
        for (std::size_t i = 1; i < splits.size(); ++i)
            GRACE_CHECK(splits[i] >= splits[i - 1]);  // monotonic
        // Derived from the camera parameters: a different far moves the splits.
        const std::vector<float> other = computeCascadeSplits(0.1f, 20.0f, 4, 0.5f);
        GRACE_CHECK(other.size() == 5u);
        GRACE_CHECK(!near(other[1], splits[1], 1e-6f));
        // Degenerate range (camera very close to the far plane): still
        // monotonic, no crash.
        const std::vector<float> tiny = computeCascadeSplits(0.1f, 0.1001f, 4, 0.5f);
        GRACE_CHECK_EQ(tiny.size(), 5u);
        for (std::size_t i = 1; i < tiny.size(); ++i)
            GRACE_CHECK(tiny[i] >= tiny[i - 1]);
        // N larger than the useful split count: clamped to the maximum.
        GRACE_CHECK_EQ(computeCascadeSplits(0.1f, 100.0f, 100, 0.5f).size(),
                       static_cast<std::size_t>(kMaxCascadeCount) + 1u);
        GRACE_CHECK_EQ(computeCascadeSplits(0.1f, 100.0f, 0, 0.5f).size(), 2u);
        // Invalid ranges rejected.
        GRACE_CHECK(throwsInvalid([] { static_cast<void>(computeCascadeSplits(0.0f, 10.0f, 4, 0.5f)); }));
        GRACE_CHECK(throwsInvalid([] { static_cast<void>(computeCascadeSplits(10.0f, 10.0f, 4, 0.5f)); }));
        GRACE_CHECK(throwsInvalid([] { static_cast<void>(computeCascadeSplits(1.0f, 10.0f, 4, 2.0f)); }));
    }

    // Cascade config validation.
    {
        CascadeConfig config;
        validateCascadeConfig(config);
        CascadeConfig bad = config;
        bad.splitLambda = 1.5f;
        GRACE_CHECK(throwsInvalid([&] { validateCascadeConfig(bad); }));
        bad = config;
        bad.mapSize = 0;
        GRACE_CHECK(throwsInvalid([&] { validateCascadeConfig(bad); }));
        bad = config;
        bad.filterTolerance = 0.0f;
        GRACE_CHECK(throwsInvalid([&] { validateCascadeConfig(bad); }));
    }

    // Slice corners: 8 per slice, farther split -> farther corners.
    {
        CameraParams camera;
        camera.aspect = 4.0f / 3.0f;
        const std::vector<vec3> nearCorners = cascadeSliceCorners(camera, 0.1f, 1.0f);
        const std::vector<vec3> farCorners = cascadeSliceCorners(camera, 10.0f, 20.0f);
        GRACE_CHECK_EQ(nearCorners.size(), 8u);
        GRACE_CHECK_EQ(farCorners.size(), 8u);
        const float nearDist = grace::core::length(nearCorners[0] - camera.eye);
        const float farDist = grace::core::length(farCorners[0] - camera.eye);
        GRACE_CHECK(farDist > nearDist);
    }

    // Trapezoidal fitting: each cascade's light matrix covers its slice's
    // corners within the documented tolerance (ndc in [-1, 1]).
    {
        CameraParams camera;
        camera.aspect = 128.0f / 96.0f;
        DirectionalLight sun;
        sun.direction = vec3{0.0f, 0.0f, -1.0f};
        const std::vector<float> splits = computeCascadeSplits(camera.near, 20.0f, 4, 0.5f);
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            const grace::core::mat4 m =
                fitCascadeLightMatrix(sun, camera, splits[i], splits[i + 1]);
            const std::vector<vec3> corners =
                cascadeSliceCorners(camera, splits[i], splits[i + 1]);
            for (const vec3& corner : corners)
            {
                const grace::core::vec4 clip =
                    m * grace::core::vec4{corner.x, corner.y, corner.z, 1.0f};
                const float nx = clip.x / clip.w;
                const float ny = clip.y / clip.w;
                const float nz = clip.z / clip.w;
                GRACE_CHECK(nx >= -1.001f && nx <= 1.001f);
                GRACE_CHECK(ny >= -1.001f && ny <= 1.001f);
                GRACE_CHECK(nz >= -1.001f && nz <= 1.001f);
            }
        }
        GRACE_CHECK(throwsInvalid([&] {
            static_cast<void>(fitCascadeLightMatrix(sun, camera, 5.0f, 1.0f));
        }));
    }

    // The full cascade pass runs through the contract on the null backend:
    // targets created, passes begun/ended, submissions drawn, no violations.
    {
        NullRenderBackend backend(128, 96);
        auto mesh = backend.createGpuMesh();
        grace::render::Mesh tri;
        grace::render::VertexLayout layout;
        layout.stride = 3 * sizeof(float);
        layout.attributes.push_back(grace::render::VertexAttribute{
            grace::render::VertexSemantic::Position, grace::render::ComponentType::Float32,
            3, 0, false});
        tri.setLayout(layout);
        tri.setVertexData(std::vector<float>{-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f});
        tri.setIndices(std::vector<std::uint32_t>{0, 1, 2});
        mesh->upload(tri);

        DrawSubmission submission;
        submission.mesh = mesh.get();
        DirectionalLight sun;
        sun.direction = vec3{0.0f, 0.0f, -1.0f};
        submission.sun = sun;

        CameraParams camera = backend.camera();
        camera.far = 20.0f;
        backend.setCamera(camera);

        CascadeConfig config;
        config.count = 4;
        config.mapSize = 64;
        grace::render::ShadowMap shadow =
            renderShadowCascades(backend, sun, camera, {submission}, config);
        GRACE_CHECK_EQ(shadow.cascadeCount, 4u);
        GRACE_CHECK_EQ(shadow.targets.size(), 4u);
        GRACE_CHECK_EQ(shadow.lightMatrices.size(), 4u);
        GRACE_CHECK_EQ(shadow.splitDepths.size(), 5u);
        GRACE_CHECK_EQ(backend.violationCount(), 0u);

        // The submission with the shadow attached is recorded as such.
        submission.shadow = &shadow;
        backend.submit(submission);
        GRACE_CHECK(backend.events().back() == std::string("submit(lights=0,sun=1,shadow=1,pointShadows=0,volume=0)"));
        GRACE_CHECK_EQ(backend.violationCount(), 0u);

        // Passes were composed through the contract.
        std::size_t begins = 0;
        std::size_t ends = 0;
        for (const auto& event : backend.events())
        {
            if (event.find("beginRenderPass(") == 0)
                ++begins;
            if (event == "endRenderPass")
                ++ends;
        }
        GRACE_CHECK_EQ(begins, 4u);
        GRACE_CHECK_EQ(ends, 4u);

        // Releasing a target, then beginning a pass on it, is a violation.
        for (grace::render::RenderTarget* target : shadow.targets)
            backend.releaseRenderTarget(target);
        backend.beginRenderPass(shadow.targets[0]);
        GRACE_CHECK_EQ(backend.violationCount(), 1u);

        backend.shutdown();
    }

    return grace::test::summary("cascades");
}
