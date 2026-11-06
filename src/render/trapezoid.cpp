#include "grace/render/trapezoid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "grace/core/math/camera.hpp"
#include "grace/render/shadow.hpp"

namespace grace::render {


bool trapezoidFromLightSpaceCorners(const std::vector<core::vec3>& lightSpaceCorners,
                                    Trapezoid& out)
{
    if (lightSpaceCorners.size() != 8)
        return false;
    // cascadeSliceCorners returns the near face first (corners[0..3]) and the
    // far face second (corners[4..7]); the same order holds after the light
    // view transform.
    core::vec2 nearMid{0.0f, 0.0f};
    core::vec2 farMid{0.0f, 0.0f};
    for (std::size_t i = 0; i < 4; ++i)
    {
        nearMid = nearMid + core::vec2{lightSpaceCorners[i].x, lightSpaceCorners[i].y};
        farMid = farMid + core::vec2{lightSpaceCorners[4 + i].x, lightSpaceCorners[4 + i].y};
    }
    nearMid = nearMid / 4.0f;
    farMid = farMid / 4.0f;

    // Axis: near-face midpoint -> far-face midpoint.
    core::vec2 axis = farMid - nearMid;
    if (core::length(axis) < 1e-4f)
        return false;  // coaxial: degenerate, caller falls back to fitted
    axis = axis / core::length(axis);
    const core::vec2 perp{-axis.y, axis.x};

    // The trapezoid's parallel edges sit at the global axis extremes; the near
    // edge's width is the near face's perpendicular extent, the far edge's is
    // the far face's — the near edge is narrower, which is what concentrates
    // texels near the viewer. Corners may slightly overhang where the faces'
    // axis ranges overlap; the shader clamps the lookup uv (documented).
    float aMin = 1e30f, aMax = -1e30f;
    for (const core::vec3& c : lightSpaceCorners)
    {
        const float a = c.x * axis.x + c.y * axis.y;
        aMin = std::fmin(aMin, a);
        aMax = std::fmax(aMax, a);
    }
    auto faceHalf = [&](std::size_t begin) {
        float half = 0.0f;
        for (std::size_t i = begin; i < begin + 4; ++i)
        {
            const core::vec2 c{lightSpaceCorners[i].x, lightSpaceCorners[i].y};
            half = std::fmax(half, std::fabs(c.x * perp.x + c.y * perp.y));
        }
        return half;
    };
    const float nearHalf = faceHalf(0);
    const float farHalf = faceHalf(4);
    constexpr float kMinExtent = 1e-4f;
    if (nearHalf < kMinExtent || farHalf < kMinExtent || (aMax - aMin) < kMinExtent)
        return false;

    // (a, p) -> xy is  a * axis + p * perp.
    out.nearLeft = axis * aMin - perp * nearHalf;
    out.nearRight = axis * aMin + perp * nearHalf;
    out.farRight = axis * aMax + perp * farHalf;
    out.farLeft = axis * aMax - perp * farHalf;
    return true;
}

bool homographyFromQuad(const core::vec2 src[4], const core::vec2 dst[4], float h[9])
{
    // Solve for (a,b,c,d,e,f,g,h) with i=1 fixed:
    //   u = (a x + b y + c) / (g x + h y + 1)
    //   v = (d x + e y + f) / (g x + h y + 1)
    // => 8 linear equations. Gaussian elimination with partial pivoting.
    float A[8][9] = {};
    for (int i = 0; i < 4; ++i)
    {
        const float x = src[i].x, y = src[i].y;
        const float u = dst[i].x, v = dst[i].y;
        A[2 * i][0] = x; A[2 * i][1] = y; A[2 * i][2] = 1.0f;
        A[2 * i][6] = -u * x; A[2 * i][7] = -u * y; A[2 * i][8] = u;
        A[2 * i + 1][3] = x; A[2 * i + 1][4] = y; A[2 * i + 1][5] = 1.0f;
        A[2 * i + 1][6] = -v * x; A[2 * i + 1][7] = -v * y; A[2 * i + 1][8] = v;
    }
    for (int col = 0; col < 8; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row)
            if (std::fabs(A[row][col]) > std::fabs(A[pivot][col]))
                pivot = row;
        if (std::fabs(A[pivot][col]) < 1e-12f)
            return false;
        if (pivot != col)
            for (int k = 0; k < 9; ++k)
                std::swap(A[col][k], A[pivot][k]);
        for (int row = 0; row < 8; ++row)
        {
            if (row == col)
                continue;
            const float f = A[row][col] / A[col][col];
            for (int k = col; k < 9; ++k)
                A[row][k] -= f * A[col][k];
        }
    }
    for (int i = 0; i < 8; ++i)
        h[i] = A[i][8] / A[i][i];
    h[8] = 1.0f;
    return true;
}

core::mat4 fitTrapezoidalCascadeLightMatrix(const DirectionalLight& sun,
                                            const CameraParams& camera, float splitNear,
                                            float splitFar, bool* fellBack)
{
    sun.validate();
    if (fellBack != nullptr)
        *fellBack = false;
    const std::vector<core::vec3> corners = cascadeSliceCorners(camera, splitNear, splitFar);

    const core::vec3 dir = core::normalise(sun.direction);
    core::vec3 center{0.0f, 0.0f, 0.0f};
    for (const core::vec3& corner : corners)
        center = center + corner;
    center = center / static_cast<float>(corners.size());
    const core::vec3 up =
        std::fabs(core::dot(dir, core::vec3{0.0f, 1.0f, 0.0f})) > 0.99f
            ? core::vec3{1.0f, 0.0f, 0.0f}
            : core::vec3{0.0f, 1.0f, 0.0f};
    // The light eye is placed far behind the slice (a directional light is
    // effectively at infinity), so the near/far faces project with a real
    // perspective ratio and the hull is a genuine trapezoid. (The fitted
    // orthographic path is insensitive to the eye distance.)
    float sliceRadius = 0.0f;
    for (const core::vec3& corner : corners)
        sliceRadius = std::fmax(sliceRadius, core::length(corner - center));
    const core::vec3 lightEye = center - dir * (10.0f * sliceRadius + 1.0f);
    const core::mat4 lightView = core::lookAt(lightEye, center, up);

    // Slice corners in light space.
    std::vector<core::vec3> lightCorners;
    lightCorners.reserve(8);
    for (const core::vec3& corner : corners)
    {
        const core::vec4 lc = lightView * core::vec4{corner.x, corner.y, corner.z, 1.0f};
        lightCorners.push_back(core::vec3{lc.x, lc.y, lc.z});
    }

    Trapezoid trap;
    if (!trapezoidFromLightSpaceCorners(lightCorners, trap))
    {
        // Documented degenerate fallback: fitted orthographic matrix.
        if (fellBack != nullptr)
            *fellBack = true;
        return fitCascadeLightMatrix(sun, camera, splitNear, splitFar);
    }

    // Ortho box tightly covering the trapezoid, with a small documented margin
    // so the quad is strictly inside.
    const core::vec2 quad[4] = {trap.nearLeft, trap.nearRight, trap.farRight, trap.farLeft};
    float minX = quad[0].x, maxX = quad[0].x, minY = quad[0].y, maxY = quad[0].y;
    for (const core::vec2& p : quad)
    {
        minX = std::fmin(minX, p.x);
        maxX = std::fmax(maxX, p.x);
        minY = std::fmin(minY, p.y);
        maxY = std::fmax(maxY, p.y);
    }
    float minZ = lightCorners[0].z, maxZ = lightCorners[0].z;
    for (const core::vec3& c : lightCorners)
    {
        minZ = std::fmin(minZ, c.z);
        maxZ = std::fmax(maxZ, c.z);
    }
    const float margin = 1e-3f * std::fmax(maxX - minX, maxY - minY) + 1e-5f;
    minX -= margin;
    maxX += margin;
    minY -= margin;
    maxY += margin;
    const float epsilon = 1e-4f + 0.01f * (maxZ - minZ);
    const float lightNear = -maxZ - epsilon;
    const float lightFar = -minZ + epsilon;
    const core::mat4 ortho =
        core::orthographic(minX, maxX, minY, maxY, lightNear, lightFar);

    // Trapezoid corners in ortho clip space (x, y in [-1, 1]).
    const core::vec2 dst[4] = {core::vec2{-1.0f, -1.0f}, core::vec2{1.0f, -1.0f},
                               core::vec2{1.0f, 1.0f}, core::vec2{-1.0f, 1.0f}};
    core::vec2 src[4];
    for (int i = 0; i < 4; ++i)
    {
        src[i].x = (quad[i].x - minX) / (maxX - minX) * 2.0f - 1.0f;
        src[i].y = (quad[i].y - minY) / (maxY - minY) * 2.0f - 1.0f;
    }
    float h[9];
    if (!homographyFromQuad(src, dst, h))
    {
        if (fellBack != nullptr)
            *fellBack = true;
        return fitCascadeLightMatrix(sun, camera, splitNear, splitFar);
    }

    // Safety: the warp may only engage when it provably contains the slice —
    // every slice corner must map inside the unit square (|ndc| <= 1) with a
    // positive homography denominator w, so nothing lands behind the
    // projection centre or gets clipped out of the map. Otherwise fall back
    // to the fitted matrix (documented).
    for (const core::vec3& c : lightCorners)
    {
        const float cx = (c.x - minX) / (maxX - minX) * 2.0f - 1.0f;
        const float cy = (c.y - minY) / (maxY - minY) * 2.0f - 1.0f;
        const float w = h[6] * cx + h[7] * cy + h[8];
        const float nx = (h[0] * cx + h[1] * cy + h[2]) / w;
        const float ny = (h[3] * cx + h[4] * cy + h[5]) / w;
        // Strict containment: every slice corner must map inside the unit
        // square with positive w, so the pass never clips slice content and
        // nothing lands behind the projection centre. Otherwise fall back.
        if (w <= 1e-3f || std::fabs(nx) > 1.001f || std::fabs(ny) > 1.001f)
        {
            if (fellBack != nullptr)
                *fellBack = true;
            return fitCascadeLightMatrix(sun, camera, splitNear, splitFar);
        }
    }

    // Embed the 2D homography as a 4x4: x,y rows from h; z row passes through;
    // w row = (g, h, 0, 1) in clip space -> projective bottom row.
    core::mat4 warp = core::mat4::identity();
    warp(0, 0) = h[0]; warp(1, 0) = h[1]; warp(2, 0) = h[2];
    warp(0, 1) = h[3]; warp(1, 1) = h[4]; warp(2, 1) = h[5];
    // z row stays (0,0,1,0); w row:
    warp(0, 3) = h[6]; warp(1, 3) = h[7]; warp(3, 3) = h[8];
    return warp * ortho * lightView;
}

}  // namespace grace::render
