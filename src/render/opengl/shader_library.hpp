#pragma once

// The OpenGL backend's shader library. RULE (decision 14): every engine and
// test shader source lives ONLY in this file, under src/render/opengl/. The
// API-neutral render module never contains shading-language source; programs
// are addressed through the API-neutral ShaderProgramId enum and resolved
// here. Adding a shader = one enum entry + one entry here.

namespace grace::render::opengl {

// Diagnostic triangle (backend-internal).
constexpr const char* kTriangleVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 mvp;
void main()
{
    gl_Position = mvp * vec4(position, 1.0);
}
)";

constexpr const char* kTriangleFragmentShader = R"(#version 330 core
uniform vec4 color;
out vec4 fragment;
void main()
{
    fragment = color;
}
)";

// Forward lighting pass.
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

constexpr const char* kLightingFragmentShader = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in float vViewDepth;
uniform vec3 cameraPos;
uniform vec3 albedo;
uniform vec3 specular;
uniform float shininess;
uniform vec3 emissive;
uniform float ambient;   // scene-global ambient term, linear (6b, decision 17)
layout(location = 1) out vec4 outAmbient;  // 6c: ambient alone, for SSAO scaling (discarded on single-attachment targets)
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
// Dual paraboloid point-light shadow maps (milestone 4d). pointLightSlot[i]
// associates dynamic light i with a map entry (or -1 = no shadow map: the
// light is applied fully, documented). Front/back are the two hemispheres.
uniform int pointShadowCount;
uniform sampler2D pointShadowFront[2];
uniform sampler2D pointShadowBack[2];
uniform mat4 pointLightViews[2];
uniform vec3 pointLightPos[2];
uniform vec2 pointLightRange[2];   // near, far (world units)
uniform float pointShadowBias[2];  // [0,1] linear depth units
uniform float pointFilterTolerance;
uniform float pointShadowMapSize;
uniform int pointLightSlot[4];
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
        if (depth > 1.0) return 1.0;  /* beyond the cascade: lit (documented) */ \
        uv = clamp(uv, 0.0, 1.0);  /* trapezoidal overhang clamps to the edge */ \
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

// Bilateral filter for the paraboloid maps: same 3x3 kernel shape as the
// cascade filter (documented), but the depth tolerance is in LINEAR [0,1]
// depth units because the paraboloid stores linear distance depth (D4 note),
// not clip depth. Sampler arrays need literal indices in GLSL 3.30, so one
// function per (light slot, hemisphere).
#define GRACE_DEFINE_PARABOLOID_SHADOW(FN, SAMP)                              \
    float FN(vec2 uv, float depth, float bias)                               \
    {                                                                        \
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth > 1.0) \
            return 1.0;  /* outside the map: lit (documented) */              \
        float weightSum = 0.0;                                               \
        float depthSum = 0.0;                                                \
        for (int oy = -1; oy <= 1; ++oy)                                     \
        {                                                                    \
            for (int ox = -1; ox <= 1; ++ox)                                 \
            {                                                                \
                float spatial =                                              \
                    (ox == 0 ? (oy == 0 ? 4.0 : 2.0) : (oy == 0 ? 2.0 : 1.0)) / 16.0; \
                vec2 offset = vec2(float(ox), float(oy)) / pointShadowMapSize; \
                float stored = texture(SAMP, uv + offset).r;                 \
                float rangeW = max(0.0, 1.0 - abs(stored - depth) / pointFilterTolerance); \
                float w = spatial * rangeW;                                  \
                weightSum += w;                                              \
                depthSum += w * stored;                                      \
            }                                                                \
        }                                                                    \
        float filtered = weightSum > 0.0 ? depthSum / weightSum             \
                                         : texture(SAMP, uv).r;              \
        return (depth <= filtered + bias) ? 1.0 : 0.0;                       \
    }

GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidFront0, pointShadowFront[0])
GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidBack0, pointShadowBack[0])
GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidFront1, pointShadowFront[1])
GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidBack1, pointShadowBack[1])

float pointLightShadow(int slot, int hemi, vec2 uv, float depth, float bias)
{
    if (slot <= 0)
        return hemi == 0 ? paraboloidFront0(uv, depth, bias) : paraboloidBack0(uv, depth, bias);
    return hemi == 0 ? paraboloidFront1(uv, depth, bias) : paraboloidBack1(uv, depth, bias);
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

    vec3 result = emissive + albedo * vec3(ambient);
    outAmbient = vec4(albedo * vec3(ambient), 1.0);
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
        // Paraboloid shadow factor for shadowed point lights (milestone 4d).
        // A light with no map (slot < 0, or a spot) gets the full light
        // (documented: shadow factor 1.0). The sun keeps its cascaded path.
        float lshadow = 1.0;
        int slot = pointLightSlot[i];
        if (slot >= 0 && lightType[i] == 0)
        {
            vec4 l = pointLightViews[slot] * vec4(vWorldPos, 1.0);
            float ldist = length(l.xyz);
            vec3 ldir = l.xyz / max(ldist, 1e-6);
            float ldepth = clamp((ldist - pointLightRange[slot].x) /
                                     (pointLightRange[slot].y - pointLightRange[slot].x),
                                 0.0, 1.0);
            int hemi = (l.z >= 0.0) ? 0 : 1;
            float u;
            float v;
            if (hemi == 0) { u = ldir.x / (1.0 + ldir.z); v = ldir.y / (1.0 + ldir.z); }
            else { u = -ldir.x / (1.0 - ldir.z); v = -ldir.y / (1.0 - ldir.z); }
            vec2 luv = vec2(u, v) * 0.5 + 0.5;
            lshadow = pointLightShadow(slot, hemi, luv, ldepth, pointShadowBias[slot]);
        }
        result += (albedo * lightColor[i] * atten * ndl
                 + specular * lightColor[i] * atten * spec * ndl) * lshadow;
    }
    fragment = vec4(result, 1.0);  // unclamped: HDR targets keep the range; 8-bit targets saturate on write
}
)";

// Cascade depth pass (shadow maps).
constexpr const char* kShadowDepthVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 lightMVP;
void main()
{
    gl_Position = lightMVP * vec4(position, 1.0);
}
)";

constexpr const char* kShadowDepthFragmentShader = R"(#version 330 core
void main()
{
}
)";

// Paraboloid depth pass (point-light shadows).
constexpr const char* kParaboloidDepthVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 lightView;    // world -> light space, light forward = +Z
uniform mat4 model;
uniform float nearRange;
uniform float farRange;
uniform int hemisphere;    // 0 = front, 1 = back
out vec3 vL;               // light-space position, interpolated exactly (w != 1)
void main()
{
    vec4 l = lightView * model * vec4(position, 1.0);
    float dist = length(l.xyz);
    vL = l.xyz;
    vec3 dir = l.xyz / max(dist, 1e-6);
    float u;
    float v;
    float w;
    if (hemisphere == 0)
    {
        if (dir.z <= 0.0) { gl_Position = vec4(0.0, 0.0, -2.0, 1.0); return; }  // clipped
        u = dir.x;
        v = dir.y;
        w = 1.0 + dir.z;
    }
    else
    {
        if (dir.z >= 0.0) { gl_Position = vec4(0.0, 0.0, -2.0, 1.0); return; }  // clipped
        u = -dir.x;
        v = -dir.y;
        w = 1.0 - dir.z;
    }
    float depth = clamp((dist - nearRange) / (farRange - nearRange), 0.0, 1.0);
    gl_Position = vec4(u * w, v * w, (2.0 * depth - 1.0) * w, w);
}
)";

constexpr const char* kParaboloidDepthFragmentShader = R"(#version 330 core
in vec3 vL;
uniform float nearRange;
uniform float farRange;
void main()
{
    float dist = length(vL);
    gl_FragDepth = clamp((dist - nearRange) / (farRange - nearRange), 0.0, 1.0);
}
)";

// Deferred G-buffer write pass (MRT).
constexpr const char* kGBufferVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 mvp;
uniform mat4 model;
out vec3 vWorldPos;
out vec3 vNormal;
void main()
{
    vec4 wp = model * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(model) * normal;
    gl_Position = mvp * vec4(position, 1.0);
}
)";

constexpr const char* kGBufferFragmentShader = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
uniform vec3 albedo;
uniform vec3 specular;
uniform float shininess;
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
void main()
{
    outAlbedo = vec4(albedo, shininess / 1024.0);  // G-buffer, unchanged
    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5,
                     dot(specular, vec3(0.299, 0.587, 0.114)));
}
)";

// Deferred lighting pass (fullscreen).
constexpr const char* kDeferredLightVertexShader = R"(#version 330 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";


constexpr const char* kDeferredLightFragmentShader = R"(#version 330 core
uniform sampler2D gbufAlbedo;
uniform sampler2D gbufNormal;
uniform sampler2D gbufDepth;
uniform mat4 invProj;
uniform mat4 invView;
uniform vec3 cameraPos;
uniform vec4 clearColor;
uniform float ambient;   // scene-global ambient term, linear (6b, decision 17)
layout(location = 1) out vec4 outAmbient;  // 6c: ambient alone, for SSAO scaling (discarded on single-attachment targets)
uniform int hasSun;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform int lightCount;
uniform int lightType[16];
// Sun cascade shadow map (consumed here since 5c) — same uniforms as forward.
uniform int hasShadowMap;
uniform int cascadeCount;
uniform sampler2D shadowMaps[4];
uniform mat4 lightMatrices[4];
uniform float splitDepths[5];
uniform float shadowBias;
uniform float filterTolerance;
uniform float shadowMapSize;
// Paraboloid point-light shadow maps (consumed since 5c).
uniform int pointShadowCount;
uniform sampler2D pointShadowFront[2];
uniform sampler2D pointShadowBack[2];
uniform mat4 pointLightViews[2];
uniform vec3 pointLightPos[2];
uniform vec2 pointLightRange[2];
uniform float pointShadowBias[2];
uniform float pointFilterTolerance;
uniform float pointShadowMapSize;
uniform int pointLightSlot[16];
uniform vec3 lightPos[16];
uniform vec3 lightDir[16];
uniform vec3 lightColor[16];
uniform float lightIntensity[16];
uniform float lightRange[16];
uniform float lightInner[16];
uniform float lightOuter[16];
out vec4 fragment;

float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}

// --- shadow lookup helpers shared by forward and deferred lighting (5c) ----
#define GRACE_DEFINE_CASCADE_SHADOW(N)                                        \
    float cascadeShadow##N(vec2 uv, float depth)                             \
    {                                                                        \
        if (depth > 1.0) return 1.0;                                          \
        uv = clamp(uv, 0.0, 1.0);                                             \
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

#define GRACE_DEFINE_PARABOLOID_SHADOW(FN, SAMP)                              \
    float FN(vec2 uv, float depth, float bias)                               \
    {                                                                        \
        if (depth > 1.0) return 1.0;                                          \
        uv = clamp(uv, 0.0, 1.0);                                             \
        float weightSum = 0.0;                                               \
        float depthSum = 0.0;                                                \
        for (int oy = -1; oy <= 1; ++oy)                                     \
        {                                                                    \
            for (int ox = -1; ox <= 1; ++ox)                                 \
            {                                                                \
                float spatial =                                              \
                    (ox == 0 ? (oy == 0 ? 4.0 : 2.0) : (oy == 0 ? 2.0 : 1.0)) / 16.0; \
                vec2 offset = vec2(float(ox), float(oy)) / pointShadowMapSize; \
                float stored = texture(SAMP, uv + offset).r;                 \
                float rangeW = max(0.0, 1.0 - abs(stored - depth) / pointFilterTolerance); \
                float w = spatial * rangeW;                                  \
                weightSum += w;                                              \
                depthSum += w * stored;                                      \
            }                                                                \
        }                                                                    \
        float filtered = weightSum > 0.0 ? depthSum / weightSum             \
                                         : texture(SAMP, uv).r;              \
        return (depth <= filtered + bias) ? 1.0 : 0.0;                       \
    }

GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidFront0, pointShadowFront[0])
GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidBack0, pointShadowBack[0])
GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidFront1, pointShadowFront[1])
GRACE_DEFINE_PARABOLOID_SHADOW(paraboloidBack1, pointShadowBack[1])

float pointLightShadow(int slot, int hemi, vec2 uv, float depth, float bias)
{
    if (slot <= 0)
        return hemi == 0 ? paraboloidFront0(uv, depth, bias) : paraboloidBack0(uv, depth, bias);
    return hemi == 0 ? paraboloidFront1(uv, depth, bias) : paraboloidBack1(uv, depth, bias);
}


void main()
{
    ivec2 size = textureSize(gbufAlbedo, 0);
    vec2 uv = gl_FragCoord.xy / vec2(size);
    float d = texture(gbufDepth, uv).r;
    if (d >= 1.0) { fragment = vec4(clearColor.rgb, 1.0); return; }  // background
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 view = invProj * clip;
    view /= view.w;
    vec4 world = invView * view;
    world /= world.w;
    vec3 N = normalize(texture(gbufNormal, uv).rgb * 2.0 - 1.0);
    vec3 albedo = texture(gbufAlbedo, uv).rgb;
    float shininess = texture(gbufAlbedo, uv).a * 1024.0;
    float specLum = texture(gbufNormal, uv).a;
    vec3 V = normalize(cameraPos - world.xyz);

    float vViewDepth = -(invView * world).z;

    // Cascade selection (same rule as forward): first cascade whose far split
    // exceeds the view depth, clamped to the last.
    int idx = 0;
    if (hasShadowMap == 1)
    {
        idx = cascadeCount - 1;
        for (int i = 0; i < cascadeCount; ++i)
        {
            if (vViewDepth < splitDepths[i + 1]) { idx = i; break; }
        }
    }

    vec3 result = albedo * vec3(ambient);
    outAmbient = vec4(albedo * vec3(ambient), 1.0);
    if (hasSun == 1)
    {
        vec3 L = normalize(-sunDirection);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        float shadow = 1.0;
        if (hasShadowMap == 1)
        {
            vec4 lp = lightMatrices[idx] * world;
            vec3 p = lp.xyz / lp.w;
            vec2 uv = p.xy * 0.5 + 0.5;
            float depth = p.z * 0.5 + 0.5;
            shadow = cascadeShadow(idx, uv, depth);
            if (idx < cascadeCount - 1)
            {
                float span = splitDepths[idx + 1] - splitDepths[idx];
                float zoneStart = splitDepths[idx + 1] - 0.25 * span;
                if (vViewDepth > zoneStart && span > 0.0)
                {
                    float t = clamp((vViewDepth - zoneStart) / (0.25 * span), 0.0, 1.0);
                    vec4 lp2 = lightMatrices[idx + 1] * world;
                    vec3 p2 = lp2.xyz / lp2.w;
                    shadow = mix(shadow, cascadeShadow(idx + 1, p2.xy * 0.5 + 0.5, p2.z * 0.5 + 0.5), t);
                }
            }
        }
        result += (albedo * sunColor * sunIntensity * ndl
                 + vec3(specLum) * sunColor * sunIntensity * spec * ndl) * shadow;
    }
    for (int i = 0; i < lightCount && i < 16; ++i)
    {
        vec3 toLight = lightPos[i] - world.xyz;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-6);
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
        float atten = rangeAtten(lightIntensity[i], lightRange[i], dist) * cone;
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        float lshadow = 1.0;
        int slot = pointLightSlot[i];
        if (slot >= 0 && lightType[i] == 0)
        {
            vec4 l = pointLightViews[slot] * world;
            float ldist = length(l.xyz);
            vec3 ldir = l.xyz / max(ldist, 1e-6);
            float ldepth = clamp((ldist - pointLightRange[slot].x) /
                                     (pointLightRange[slot].y - pointLightRange[slot].x), 0.0, 1.0);
            int hemi = (l.z >= 0.0) ? 0 : 1;
            float u;
            float v;
            if (hemi == 0) { u = ldir.x / (1.0 + ldir.z); v = ldir.y / (1.0 + ldir.z); }
            else { u = -ldir.x / (1.0 - ldir.z); v = -ldir.y / (1.0 - ldir.z); }
            vec2 luv = vec2(u, v) * 0.5 + 0.5;
            lshadow = pointLightShadow(slot, hemi, luv, ldepth, pointShadowBias[slot]);
        }
        result += (albedo * lightColor[i] * atten * ndl
                 + vec3(specLum) * lightColor[i] * atten * spec * ndl) * lshadow;
    }
    fragment = vec4(result, 1.0);  // unclamped: HDR targets keep the range; 8-bit targets saturate on write
}
)";

// Test fixture: flat unlit (position passthrough + uniform colour).
constexpr const char* kFlatVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(position, 1.0); }
)";

constexpr const char* kFlatFragmentShader = R"(#version 330 core
uniform vec4 color;
out vec4 fragment;
void main() { fragment = color; }
)";

// Test fixtures: deliberately broken stages, for error-path coverage only.
constexpr const char* kBrokenVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(broken_identifier_xyz, 1.0); }
)";

constexpr const char* kBrokenFragmentShader = R"(#version 330 core
out vec4 fragment;
void main() { fragment = color_uniform_that_does_not_exist; }
)";

// Post-processing chain (milestone 6a), all fullscreen passes reusing the
// deferred fullscreen vertex stage. HDR values are linear; the tonemap stage
// is the linear->display boundary: filmic ACES approximation, then sRGB encode.
constexpr const char* kPostBrightFragmentShader = R"(#version 330 core
uniform sampler2D srcTex;
uniform vec2 outSize;   // current render target size; uv must map over it
uniform float threshold;
out vec4 fragment;
void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec3 c = texture(srcTex, uv).rgb;
    fragment = vec4(max(c - vec3(threshold), vec3(0.0)), 1.0);
}
)";

constexpr const char* kPostBlurFragmentShader = R"(#version 330 core
uniform sampler2D srcTex;
uniform vec2 outSize;     // current render target size
uniform vec2 direction;   // (1,0) or (0,1) in texels of the source level
out vec4 fragment;
void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec2 step = direction / vec2(textureSize(srcTex, 0));
    // 9-tap Gaussian weights (1 2 1 / 2 4 2 / 1 2 1) normalized by 16, with the
    // two outer taps folded in: 0.227027 + 2*0.194594 + 2*0.121621 + 2*0.054054
    vec3 sum = texture(srcTex, uv).rgb * 0.227027;
    sum += texture(srcTex, uv + step * 1.3846153846).rgb * 0.3162162162;
    sum += texture(srcTex, uv - step * 1.3846153846).rgb * 0.3162162162;
    sum += texture(srcTex, uv + step * 3.2307692308).rgb * 0.0702702703;
    sum += texture(srcTex, uv - step * 3.2307692308).rgb * 0.0702702703;
    fragment = vec4(sum, 1.0);
}
)";

constexpr const char* kPostTonemapFragmentShader = R"(#version 330 core
uniform sampler2D hdrTex;     // linear HDR lighting result (direct + ambient)
uniform sampler2D hdrAmbient; // linear ambient-only attachment (6c)
uniform sampler2D bloomTex;   // linear bloom (already blurred)
uniform sampler2D ssaoTex;    // occlusion factor in [0,1] (1 = unoccluded)
uniform int useSSAO;          // 0 when the SSAO pass was skipped: occlusion is 1
uniform vec2 outSize;         // default framebuffer size
uniform float exposure;
out vec4 fragment;

// Filmic tonemap (architecture 3.2 "filmic curve"): ACES approximation
// (Narkowicz). Low end: near-linear, keeps shadow detail. Middle: gentle
// shoulder. Above 1: rolls off to 1, compressing highlights at the cost of
// some highlight saturation.
vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Linear -> sRGB display encode: this is the linear-to-display boundary (H0).
vec3 srgbEncode(vec3 c)
{
    return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, c * 12.92,
               step(c, vec3(0.0031308)));
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec3 hdr = texture(hdrTex, uv).rgb;          // direct + ambient
    vec3 amb = texture(hdrAmbient, uv).rgb;      // ambient alone (attachment 1; 0 on single-attachment targets)
    vec3 bloom = texture(bloomTex, uv).rgb;
    float occ = useSSAO == 1 ? texture(ssaoTex, uv).r : 1.0;
    // 6c: occlusion multiplies the ambient term only: direct + ambient * occ.
    vec3 tonemapped = aces((hdr - amb + amb * occ + bloom) * exposure);
    fragment = vec4(srgbEncode(tonemapped), 1.0);
}
)";

// SSAO pass (milestone 6c): fullscreen pass over the HDR target's depth texture.
// View-space position is reconstructed from depth; the normal is reconstructed
// from the depth gradient (screen-space derivatives — degrades at grazing
// angles, documented in decision 18). Samples a fixed hemisphere pattern around
// the normal within `radius` world units; self-occlusion is prevented by the
// normal bias. Output is the occlusion factor in [0,1] to an R8 buffer.
// Deterministic: the sample pattern is fixed (no per-frame random rotation), so
// tests are reproducible. Temporal filtering (SSAO-STF) is NOT implemented here.
constexpr const char* kPostSSAOFragmentShader = R"(#version 330 core
uniform sampler2D depthTex;
uniform mat4 invProj;
uniform mat4 proj;
uniform vec2 outSize;
uniform float radius;      // world units
uniform int sampleCount;
uniform float bias;        // world units along the normal
out vec4 fragment;

vec3 viewPos(vec2 uv, float d)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = invProj * clip;
    return v.xyz / v.w;
}

// fixed hemisphere sample directions (tangent space, z >= 0), 8 samples
vec3 sampleDir(int i)
{
    if (i == 0) return vec3(0.0, 0.0, 1.0);
    if (i == 1) return vec3(0.7071, 0.0, 0.7071);
    if (i == 2) return vec3(0.0, 0.7071, 0.7071);
    if (i == 3) return vec3(-0.7071, 0.0, 0.7071);
    if (i == 4) return vec3(0.0, -0.7071, 0.7071);
    if (i == 5) return vec3(0.5, 0.5, 0.7071);
    if (i == 6) return vec3(-0.5, 0.5, 0.7071);
    return vec3(0.5, -0.5, 0.7071);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    float d = texture(depthTex, uv).r;
    if (d >= 1.0) { fragment = vec4(1.0); return; }  // background: unoccluded
    vec3 p = viewPos(uv, d);
    vec2 texel = 1.0 / outSize;
    vec3 px = viewPos(uv + vec2(texel.x, 0.0), texture(depthTex, uv + vec2(texel.x, 0.0)).r);
    vec3 py = viewPos(uv + vec2(0.0, texel.y), texture(depthTex, uv + vec2(0.0, texel.y)).r);
    vec3 n = normalize(cross(px - p, py - p));
    vec3 t = normalize(cross(n, vec3(0.0, 1.0, 0.0)) + vec3(1e-6));
    vec3 b = cross(n, t);

    float occluded = 0.0;
    for (int i = 0; i < sampleCount && i < 8; ++i)
    {
        vec3 dir = normalize(sampleDir(i));
        float scale = radius * (0.1 + 0.9 * float(i + 1) / 8.0);
        vec3 sp = p + (t * dir.x + b * dir.y + n * dir.z) * scale + n * bias;
        vec4 clip = proj * vec4(sp, 1.0);
        if (clip.w <= 0.0)
            continue;  // sample behind the eye
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;
        float sd = texture(depthTex, suv).r;
        vec3 sv = viewPos(suv, sd);
        // occluded if the stored surface at the sample is closer than the sample point
        if (sv.z > sp.z)
            occluded += 1.0;
    }
    float occ = 1.0 - occluded / float(min(sampleCount, 8));
    fragment = vec4(occ);
}
)";

// Morphological anti-aliasing (milestone 6d; moved to the display-encoded
// image in 6e, decision 22 update). The post chain ends in an offscreen 8-bit
// target (the display target), which IS sampleable, so MLAA runs here on the
// display-encoded image where a luminance threshold means the same thing in
// shadows and highlights (on linear HDR the same threshold is generous in the
// dark and stingy in the light). A simple morphological pass in the shape of
// MLAA's edge detection — NOT SMAA: no pattern search, no reprojection, one
// pass. Edges: Rec709 luminance contrast of the DISPLAY codes (threshold in
// [0,1] display units) or NDC depth contrast. Each contrasting neighbour adds
// its colour to an equal-weight average. A single-pixel edge blends to about
// half intensity; a high-contrast interior boundary (bright HUD edge) blends
// too — the stated cost of the simple morphology.
constexpr const char* kPostMLAAFragmentShader = R"(#version 330 core
uniform sampler2D srcTex;       // display-encoded colour (8-bit target, [0,1])
uniform sampler2D depthTex;     // scene depth (NDC [0,1]), from the HDR target
uniform vec2 outSize;
uniform float lumThreshold;     // display-code luminance delta marking an edge
uniform float depthThreshold;   // NDC depth delta marking an edge
out vec4 fragment;

float luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec2 t = 1.0 / outSize;
    vec3 c = texture(srcTex, uv).rgb;
    float d = texture(depthTex, uv).r;
    float lc = luminance(c);

    vec2 offsets[4];
    offsets[0] = vec2(0.0, t.y);
    offsets[1] = vec2(0.0, -t.y);
    offsets[2] = vec2(t.x, 0.0);
    offsets[3] = vec2(-t.x, 0.0);

    vec3 sum = c;
    float count = 1.0;
    for (int i = 0; i < 4; ++i)
    {
        vec3 nc = texture(srcTex, uv + offsets[i]).rgb;
        float nd = texture(depthTex, uv + offsets[i]).r;
        if (abs(luminance(nc) - lc) > lumThreshold || abs(nd - d) > depthThreshold)
        {
            sum += nc;
            count += 1.0;
        }
    }
    fragment = vec4(sum / count, 1.0);
}
)";

// Present pass (milestone 6e, decision 23): the chain ends in the offscreen
// 8-bit display target; this textured fullscreen copy presents it onto the
// default framebuffer, which is what swapBuffers then shows. A device blit
// composed from the generic pass primitives (beginRenderPass(nullptr) +
// bindTexture + draw) — no contract change.
constexpr const char* kPostBlitFragmentShader = R"(#version 330 core
uniform sampler2D srcTex;   // the display target's colour attachment
out vec4 fragment;
void main()
{
    fragment = texture(srcTex, gl_FragCoord.xy / vec2(textureSize(srcTex, 0)));
}
)";

// Depth of field with bokeh (milestone 6d, decision 21). Runs in LINEAR HDR
// before the tonemap (blurring light, not display codes). The circle of
// confusion is derived from the scene depth: view distance d from the depth
// buffer via invProj (camera looks down -Z), coc = clamp(aperture * |d -
// focusDistance| / d, 0, maxCocPixels) — focusDistance and aperture in world
// units, maxCocPixels the clamp in pixels. The blur is a 16-tap fixed DISC
// (bokeh), a single gather pass, NOT separable: a separable blur would be a
// Gaussian cross, and a disc costs the full 2D tap count (bounded by the CoC
// clamp). Silhouette measure: a tap contributes only if its own view distance
// silhouette: a tap closer to the eye than the centre pixel is rejected, so a
// blurred foreground object cannot bleed over a sharp background; taps behind
// the centre are accepted, which is what spreads a defocused surface's light
// into the bokeh disc. Both HDR attachments (colour and ambient) are blurred
// with the same weights so the tonemap compose stays consistent. No temporal
// or physically-based accumulation.
constexpr const char* kPostDOFFragmentShader = R"(#version 330 core
uniform sampler2D srcTex;        // linear HDR colour (direct + ambient)
uniform sampler2D srcAmbient;    // linear ambient-only attachment
uniform sampler2D depthTex;      // scene depth (NDC [0,1])
uniform mat4 invProj;
uniform vec2 outSize;
uniform float tanHalfFovY;       // tan(fovY / 2), for the world->pixel conversion
uniform float focusDistance;     // world units: the sharp plane
uniform float aperture;          // world units: CoC scale
uniform float maxCocPixels;      // clamp, pixels
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmb;

vec3 viewPos(vec2 uv, float d)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = invProj * clip;
    return v.xyz / v.w;
}

// 16 fixed taps on the unit disc: centre + 5 at radius 0.42 + 10 at radius 0.85
vec2 discTap(int i)
{
    if (i == 0)
        return vec2(0.0, 0.0);
    int j = i - 1;
    if (j < 5)
    {
        float ang = 6.2831853 * float(j) / 5.0 + 0.3;
        return vec2(cos(ang), sin(ang)) * 0.42;
    }
    int k = j - 5;
    float ang = 6.2831853 * float(k) / 10.0 + 0.11;
    return vec2(cos(ang), sin(ang)) * 0.85;
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec2 t = 1.0 / outSize;
    float d = texture(depthTex, uv).r;
    if (d >= 1.0)  // background: copy through
    {
        outColor = texture(srcTex, uv);
        outAmb = texture(srcAmbient, uv);
        return;
    }
    vec3 p = viewPos(uv, d);
    float dist = -p.z;  // view distance: camera looks down -Z
    // Circle of confusion (documented formula and clamp, decision 21): the
    // world-space CoC aperture*|d-focus|/d converted to pixels at this
    // distance (worldPerPixel = 2*d*tan(fovY/2)/height), clamped to maxCocPixels.
    float worldPerPixel = 2.0 * dist * tanHalfFovY / outSize.y;
    float coc = clamp(aperture * abs(dist - focusDistance) / max(dist, 0.05) / worldPerPixel,
                      0.0, maxCocPixels);
    if (coc < 0.5)  // sharp: copy through
    {
        outColor = texture(srcTex, uv);
        outAmb = texture(srcAmbient, uv);
        return;
    }
    vec3 sum = vec3(0.0);
    vec3 asum = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        vec2 suv = uv + discTap(i) * coc * t;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;
        float sd = texture(depthTex, suv).r;
        float sdist = -viewPos(suv, sd).z;
        // Silhouette guard: reject taps closer to the eye than the centre
        // (a blurred foreground must not bleed over a sharp background);
        // taps behind the centre are accepted (bokeh spread).
        float w = sdist >= dist - 0.02 ? 1.0 : 0.0;
        sum += w * texture(srcTex, suv).rgb;
        asum += w * texture(srcAmbient, suv).rgb;
        wsum += w;
    }
    if (wsum < 0.5)
    {
        outColor = texture(srcTex, uv);
        outAmb = texture(srcAmbient, uv);
        return;
    }
    outColor = vec4(sum / wsum, 1.0);
    outAmb = vec4(asum / wsum, 1.0);
}
)";

}  // namespace grace::render::opengl
