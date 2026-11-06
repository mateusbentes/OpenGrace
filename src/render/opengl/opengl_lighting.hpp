#pragma once

// Internal GLSL sources for the forward lighting pass of the OpenGL backend.
// This header lives in src/ and is not part of any public API-neutral header.

namespace grace::render::opengl {

// Vertex stage: transforms by model/mvp/view and passes world position,
// normal and view depth to the fragment stage. Normals use mat3(model),
// which is exact for rotation/uniform scale (the documented limitation for
// non-uniform scale).
constexpr const char* kLightingVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 mvp;
uniform mat4 model;
uniform mat4 viewMatrix;
out vec3 vWorldPos;
out vec3 vNormal;
out float vViewDepth;
void main()
{
    vec4 wp = model * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(model) * normal;
    vec4 vp = viewMatrix * wp;
    vViewDepth = -vp.z;
    gl_Position = mvp * vec4(position, 1.0);
}
)";

// Fragment stage: Lambert diffuse + Blinn-Phong specular. The sun
// (directional light) is handled separately from the dynamic lights, as the
// architecture document describes, and is the only shadowed term (milestone
// 4c; dynamic lights stay unshadowed, documented). Cascade selection uses the
// fragment's view depth against splitDepths (documented rule: the first
// cascade whose far split exceeds the depth). The shadow lookup uses a
// bilateral 3x3 filter: spatial Gaussian weights times a depth-range weight,
// with filterTolerance as the documented depth-comparison tolerance — not a
// plain PCF. Fragments in the last 25% of a cascade's range are blended with
// the next cascade to avoid a visible seam at the cascade boundary.
constexpr const char* kLightingFragmentShader = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in float vViewDepth;
uniform vec3 cameraPos;
uniform vec3 albedo;
uniform vec3 specular;
uniform float shininess;
uniform vec3 emissive;
uniform int hasSun;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform int lightCount;
uniform int lightType[4];
uniform vec3 lightPos[4];
uniform vec3 lightDir[4];
uniform vec3 lightColor[4];
uniform float lightIntensity[4];
uniform float lightRange[4];
uniform float lightInner[4];
uniform float lightOuter[4];
// Cascaded sun shadow map (milestone 4c), sun term only.
uniform int hasShadowMap;
uniform int cascadeCount;
uniform sampler2D shadowMaps[4];
uniform mat4 lightMatrices[4];
uniform float splitDepths[5];
uniform float shadowBias;
uniform float filterTolerance;
uniform float shadowMapSize;  // per-cascade resolution, for the filter's texel step
uniform int debugCascades;
out vec4 fragment;

float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}

// One cascade's shadow factor at the fragment, with the bilateral filter.
// uv/depth are this cascade's light-space projection of the fragment.
// GLSL 3.30 forbids indexing sampler arrays with non-constant expressions, so
// each cascade gets its own function with a literal sampler index.
#define GRACE_DEFINE_CASCADE_SHADOW(N)                                        \
    float cascadeShadow##N(vec2 uv, float depth)                             \
    {                                                                        \
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth > 1.0) \
            return 1.0;  /* outside the cascade's map: lit (documented) */   \
        /* 3x3 bilateral kernel: spatial Gaussian (1 2 1 / 2 4 2 / 1 2 1)/16  \
           times a depth-range weight that suppresses samples across depth  \
           discontinuities. */                                             \
        float weightSum = 0.0;                                               \
        float depthSum = 0.0;                                                \
        for (int oy = -1; oy <= 1; ++oy)                                     \
        {                                                                    \
            for (int ox = -1; ox <= 1; ++ox)                                 \
            {                                                                \
                float spatial =                                              \
                    (ox == 0 ? (oy == 0 ? 4.0 : 2.0) : (oy == 0 ? 2.0 : 1.0)) / 16.0; \
                vec2 offset = vec2(float(ox), float(oy)) / shadowMapSize;    \
                float stored = texture(shadowMaps[N], uv + offset).r;       \
                float rangeW = max(0.0, 1.0 - abs(stored - depth) / filterTolerance); \
                float w = spatial * rangeW;                                  \
                weightSum += w;                                              \
                depthSum += w * stored;                                      \
            }                                                                \
        }                                                                    \
        float filtered = weightSum > 0.0 ? depthSum / weightSum             \
                                         : texture(shadowMaps[N], uv).r;     \
        return (depth <= filtered + shadowBias) ? 1.0 : 0.0;                 \
    }

GRACE_DEFINE_CASCADE_SHADOW(0)
GRACE_DEFINE_CASCADE_SHADOW(1)
GRACE_DEFINE_CASCADE_SHADOW(2)
GRACE_DEFINE_CASCADE_SHADOW(3)

float cascadeShadow(int idx, vec2 uv, float depth)
{
    if (idx <= 0) return cascadeShadow0(uv, depth);
    if (idx == 1) return cascadeShadow1(uv, depth);
    if (idx == 2) return cascadeShadow2(uv, depth);
    return cascadeShadow3(uv, depth);
}

// Light-space uv/depth of the fragment in cascade i.
vec3 lightProj(int i)
{
    vec4 lp = lightMatrices[i] * vec4(vWorldPos, 1.0);
    vec3 p = lp.xyz / lp.w;
    return vec3(p.xy * 0.5 + 0.5, p.z * 0.5 + 0.5);
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPos - vWorldPos);

    // Cascade selection (documented): first cascade whose far split exceeds
    // the fragment's view depth; clamped to the last cascade.
    int idx = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (vViewDepth < splitDepths[i + 1]) { idx = i; break; }
    }

    // Diagnostics (C5): visualise the cascade index, off by default.
    if (debugCascades == 1)
    {
        float t = float(idx) / float(max(cascadeCount, 1));
        fragment = vec4(t, 0.0, 1.0 - t, 1.0);
        return;
    }

    vec3 result = emissive;
    if (hasSun == 1)
    {
        vec3 L = normalize(-sunDirection);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        float shadow = 1.0;
        if (hasShadowMap == 1)
        {
            vec3 p0 = lightProj(idx);
            shadow = cascadeShadow(idx, p0.xy, p0.z);
            // Blend zone: last 25% of the cascade's range blends into the next
            // cascade, so a fragment near the boundary sees no seam.
            if (idx < cascadeCount - 1)
            {
                float span = splitDepths[idx + 1] - splitDepths[idx];
                float zoneStart = splitDepths[idx + 1] - 0.25 * span;
                if (vViewDepth > zoneStart && span > 0.0)
                {
                    float t = clamp((vViewDepth - zoneStart) / (0.25 * span), 0.0, 1.0);
                    vec3 p1 = lightProj(idx + 1);
                    shadow = mix(shadow, cascadeShadow(idx + 1, p1.xy, p1.z), t);
                }
            }
        }
        result += (albedo * sunColor * sunIntensity * ndl
                 + specular * sunColor * sunIntensity * spec * ndl) * shadow;
    }
    for (int i = 0; i < lightCount && i < 4; ++i)
    {
        vec3 toLight = lightPos[i] - vWorldPos;
        float d = length(toLight);
        vec3 L = toLight / max(d, 1e-6);
        float ndl = max(dot(N, L), 0.0);
        float cone = 1.0;
        if (lightType[i] == 1)
        {
            float cosAngle = dot(-L, normalize(lightDir[i]));
            float cosInner = cos(lightInner[i]);
            float cosOuter = cos(lightOuter[i]);
            float t = clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0.0, 1.0);
            cone = t * t * (3.0 - 2.0 * t);
        }
        float atten = rangeAtten(lightIntensity[i], lightRange[i], d) * cone;
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        result += albedo * lightColor[i] * atten * ndl
                + specular * lightColor[i] * atten * spec * ndl;
    }
    fragment = vec4(clamp(result, 0.0, 1.0), 1.0);
}
)";

}  // namespace grace::render::opengl
