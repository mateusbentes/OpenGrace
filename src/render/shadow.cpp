#include "grace/render/shadow.hpp"

#include <cmath>
#include <stdexcept>

#include "grace/core/math/camera.hpp"
#include "grace/render/render_backend.hpp"
#include "grace/render/shader.hpp"

namespace grace::render {


core::mat4 sunLightSpaceMatrix(const DirectionalLight& sun, const SceneBounds& bounds,
                               float near, float far)
{
    sun.validate();  // rejects a zero-length direction with a clear message
    if (!(bounds.radius > 0.0f))
        throw std::invalid_argument("shadow: scene bounds radius must be > 0");
    if (!(near >= 0.0f))
        throw std::invalid_argument("shadow: near must be >= 0");
    if (!(far > near))
        throw std::invalid_argument("shadow: far must be > near");

    const core::vec3 dir = core::normalise(sun.direction);
    // Eye placed so the near plane touches the light-side extreme of the
    // bounding sphere; every scene point is then in front of the camera.
    const core::vec3 eye = bounds.center - dir * (near + bounds.radius);
    const core::vec3 up =
        std::fabs(core::dot(dir, core::vec3{0.0f, 1.0f, 0.0f})) > 0.99f
            ? core::vec3{1.0f, 0.0f, 0.0f}
            : core::vec3{0.0f, 1.0f, 0.0f};
    const core::mat4 view = core::lookAt(eye, bounds.center, up);
    const core::mat4 proj = core::orthographic(-bounds.radius, bounds.radius,
                                               -bounds.radius, bounds.radius, near, far);
    return proj * view;
}

void validateCascadeConfig(const CascadeConfig& config)
{
    if (config.mapSize == 0)
        throw std::invalid_argument("cascade config: mapSize must be > 0");
    if (!(config.splitLambda >= 0.0f) || !(config.splitLambda <= 1.0f))
        throw std::invalid_argument("cascade config: splitLambda must be in [0, 1]");
    if (!(config.bias >= 0.0f))
        throw std::invalid_argument("cascade config: bias must be >= 0");
    if (!(config.filterTolerance > 0.0f))
        throw std::invalid_argument("cascade config: filterTolerance must be > 0");
}

std::vector<float> computeCascadeSplits(float near, float far, std::uint32_t count,
                                        float lambda)
{
    if (!(near > 0.0f))
        throw std::invalid_argument("cascade splits: near must be > 0");
    if (!(far > near))
        throw std::invalid_argument("cascade splits: far must be > near");
    if (!(lambda >= 0.0f) || !(lambda <= 1.0f))
        throw std::invalid_argument("cascade splits: lambda must be in [0, 1]");
    if (count == 0)
        count = 1;
    if (count > kMaxCascadeCount)
        count = kMaxCascadeCount;  // documented clamp: N larger than the useful
                                   // split count collapses to the maximum

    std::vector<float> splits;
    splits.reserve(count + 1);
    splits.push_back(near);
    float previous = near;
    const float ratio = far / near;
    for (std::uint32_t i = 1; i < count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float logSplit = near * std::pow(ratio, t);
        const float uniformSplit = near + (far - near) * t;
        float split = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        if (split < previous)
            split = previous;  // keep monotonic even for tiny ranges
        if (split > far)
            split = far;
        splits.push_back(split);
        previous = split;
    }
    splits.push_back(far);
    return splits;
}

std::vector<core::vec3> cascadeSliceCorners(const CameraParams& camera, float splitNear,
                                            float splitFar)
{
    camera.validate();
    if (!(splitFar > splitNear) || !(splitNear >= camera.near) || !(splitFar <= camera.far))
        throw std::invalid_argument("cascade slice: need camera.near <= splitNear < splitFar <= camera.far");

    const core::mat4 view =
        core::lookAt(camera.eye, camera.target, camera.up);
    const core::mat4 viewInv = core::inverseAffine(view);
    const float tanHalf = std::tan(camera.fovYRad * 0.5f);

    std::vector<core::vec3> corners;
    corners.reserve(8);
    for (const float depth : {splitNear, splitFar})
    {
        const float halfH = depth * tanHalf;
        const float halfW = halfH * camera.aspect;
        for (const float sx : {-1.0f, 1.0f})
            for (const float sy : {-1.0f, 1.0f})
            {
                const core::vec4 viewCorner{sx * halfW, sy * halfH, -depth, 1.0f};
                const core::vec4 world = viewInv * viewCorner;
                corners.push_back(core::vec3{world.x / world.w, world.y / world.w,
                                             world.z / world.w});
            }
    }
    return corners;
}

core::mat4 fitCascadeLightMatrix(const DirectionalLight& sun, const CameraParams& camera,
                                 float splitNear, float splitFar)
{
    sun.validate();
    const std::vector<core::vec3> corners = cascadeSliceCorners(camera, splitNear, splitFar);

    // Light view oriented along the sun direction. The eye offset along the
    // light direction does not matter for an orthographic fit (the depth
    // range is recomputed from the corners), so a unit offset is used.
    const core::vec3 dir = core::normalise(sun.direction);
    core::vec3 center{0.0f, 0.0f, 0.0f};
    for (const core::vec3& corner : corners)
        center = center + corner;
    center = center / static_cast<float>(corners.size());
    const core::vec3 up =
        std::fabs(core::dot(dir, core::vec3{0.0f, 1.0f, 0.0f})) > 0.99f
            ? core::vec3{1.0f, 0.0f, 0.0f}
            : core::vec3{0.0f, 1.0f, 0.0f};
    const core::mat4 lightView = core::lookAt(center - dir, center, up);

    // Trapezoidal fit: min/max of the corners in light space.
    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f, minZ = 0.0f, maxZ = 0.0f;
    bool first = true;
    for (const core::vec3& corner : corners)
    {
        const core::vec4 lc = lightView * core::vec4{corner.x, corner.y, corner.z, 1.0f};
        if (first)
        {
            minX = maxX = lc.x;
            minY = maxY = lc.y;
            minZ = maxZ = lc.z;
            first = false;
        }
        else
        {
            minX = std::fmin(minX, lc.x);
            maxX = std::fmax(maxX, lc.x);
            minY = std::fmin(minY, lc.y);
            maxY = std::fmax(maxY, lc.y);
            minZ = std::fmin(minZ, lc.z);
            maxZ = std::fmax(maxZ, lc.z);
        }
    }
    // Light-space view looks along -Z: distances along the light direction are
    // -z, so near = -maxZ (farthest corner from the light eye) and
    // far = -minZ. A small epsilon keeps the range non-degenerate.
    const float epsilon = 1e-4f + 0.01f * (maxZ - minZ);
    const float lightNear = -maxZ - epsilon;
    const float lightFar = -minZ + epsilon;
    const core::mat4 proj =
        core::orthographic(minX, maxX, minY, maxY, lightNear, lightFar);
    return proj * lightView;
}

ShadowMap renderShadowCascades(RenderBackend& backend, const DirectionalLight& sun,
                               const CameraParams& camera,
                               const std::vector<DrawSubmission>& submissions,
                               const CascadeConfig& config, std::uint32_t maxDynamicLights)
{
    validateCascadeConfig(config);
    camera.validate();
    sun.validate();
    for (const DrawSubmission& submission : submissions)
        submission.validate(maxDynamicLights);

    std::uint32_t count = config.count;
    if (count == 0)
        count = 1;
    if (count > kMaxCascadeCount)
        count = kMaxCascadeCount;

    const std::vector<float> splits =
        computeCascadeSplits(camera.near, camera.far, count, config.splitLambda);

    auto depthProgram = backend.createShaderProgram(ShaderProgramId::ShadowDepth);

    ShadowMap shadow;
    shadow.cascadeCount = count;
    shadow.mapSize = config.mapSize;
    shadow.bias = config.bias;
    shadow.filterTolerance = config.filterTolerance;
    shadow.debug = config.debug;
    shadow.splitDepths = splits;
    shadow.targets.reserve(count);
    shadow.lightMatrices.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i)
    {
        RenderTargetDesc desc;
        desc.width = config.mapSize;
        desc.height = config.mapSize;
        desc.colorAttachmentCount = 0;  // depth-only: what a shadow pass needs
        desc.depthAttachment = true;
        desc.depthFormat = PixelFormat::Depth24Stencil8;
        RenderTarget* target = backend.createRenderTarget(desc);

        const core::mat4 lightMatrix =
            config.projection == CascadeProjection::Trapezoidal
                ? fitTrapezoidalCascadeLightMatrix(sun, camera, splits[i], splits[i + 1],
                                                   nullptr)
                : fitCascadeLightMatrix(sun, camera, splits[i], splits[i + 1]);

        // Generic pass composition through the contract.
        backend.beginRenderPass(target);
        backend.clear();
        depthProgram->use();
        for (const DrawSubmission& submission : submissions)
        {
            const core::mat4 lightMVP = lightMatrix * submission.model;
            depthProgram->setUniform("lightMVP", lightMVP);
            submission.mesh->drawIndexed();
        }
        backend.endRenderPass();

        shadow.targets.push_back(target);
        shadow.lightMatrices.push_back(lightMatrix);
    }
    return shadow;
}

void validatePointShadowConfig(const PointShadowConfig& config)
{
    if (config.mapSize == 0)
        throw std::invalid_argument("point shadow config: mapSize must be > 0");
    if (!(config.near > 0.0f))
        throw std::invalid_argument("point shadow config: near must be > 0");
    if (!(config.far > config.near))
        throw std::invalid_argument("point shadow config: far must be > near");
    if (!(config.biasWorld >= 0.0f))
        throw std::invalid_argument("point shadow config: biasWorld must be >= 0");
    if (!(config.filterTolerance > 0.0f))
        throw std::invalid_argument("point shadow config: filterTolerance must be > 0");
}

PointShadowMaps renderPointShadowMaps(RenderBackend& backend,
                                       const std::vector<DynamicLight>& lights,
                                       const std::vector<DrawSubmission>& casters,
                                       const PointShadowConfig& config,
                                       std::uint32_t maxDynamicLights)
{
    validatePointShadowConfig(config);
    for (const DrawSubmission& caster : casters)
        caster.validate(maxDynamicLights);

    std::uint32_t maxLights = config.maxLights;
    if (maxLights == 0)
        maxLights = 1;
    if (maxLights > kMaxShadowCastingPointLights)
        maxLights = kMaxShadowCastingPointLights;  // documented budget clamp

    // Collect the point lights to shadow (first maxLights point lights).
    std::vector<std::uint32_t> pointLightIndices;
    for (std::uint32_t i = 0; i < lights.size() && pointLightIndices.size() < maxLights; ++i)
        if (std::holds_alternative<PointLight>(lights[i]))
            pointLightIndices.push_back(i);

    auto depthProgram = backend.createShaderProgram(ShaderProgramId::ParaboloidDepth);

    PointShadowMaps result;
    result.mapSize = config.mapSize;
    result.biasWorld = config.biasWorld;
    result.filterTolerance = config.filterTolerance;

    for (const std::uint32_t lightIndex : pointLightIndices)
    {
        const PointLight& light = std::get<PointLight>(lights[lightIndex]);
        light.validate();

        PointShadowMapEntry entry;
        entry.lightIndex = lightIndex;
        entry.near = config.near;
        entry.far = config.far;
        // Point lights emit in all directions; the light's own "forward" is
        // arbitrary for the paraboloid (the two hemispheres cover the sphere),
        // so +Z with an arbitrary non-parallel up is used (documented).
        entry.lightView = paraboloidLightView(light.position, core::vec3{0.0f, 0.0f, 1.0f},
                                              core::vec3{0.0f, 1.0f, 0.0f});

        // Two targets per light: one per hemisphere (documented D2 choice —
        // one target with two viewports would need a viewport API in the
        // contract, which decision 7 forbids adding for one technique).
        RenderTarget* targets[2] = {nullptr, nullptr};
        for (int hemi = 0; hemi < 2; ++hemi)
        {
            RenderTargetDesc desc;
            desc.width = config.mapSize;
            desc.height = config.mapSize;
            desc.colorAttachmentCount = 0;  // depth-only
            desc.depthAttachment = true;
            desc.depthFormat = PixelFormat::Depth24Stencil8;
            targets[hemi] = backend.createRenderTarget(desc);

            backend.beginRenderPass(targets[hemi]);
            backend.clear();
            depthProgram->use();
            depthProgram->setUniform("lightView", entry.lightView);
            depthProgram->setUniform("nearRange", config.near);
            depthProgram->setUniform("farRange", config.far);
            depthProgram->setUniform("hemisphere", hemi);
            for (const DrawSubmission& caster : casters)
            {
                depthProgram->setUniform("model", caster.model);
                caster.mesh->drawIndexed();
            }
            backend.endRenderPass();
        }
        entry.front = targets[0];
        entry.back = targets[1];
        result.entries.push_back(entry);
    }
    return result;
}

}  // namespace grace::render
