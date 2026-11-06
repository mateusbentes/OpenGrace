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
    outAlbedo = vec4(albedo, shininess / 1024.0);
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
uniform sampler2D hdrTex;     // linear HDR lighting result
uniform sampler2D bloomTex;   // linear bloom (already blurred)
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
    vec3 hdr = texture(hdrTex, uv).rgb;
    vec3 bloom = texture(bloomTex, uv).rgb;
    vec3 tonemapped = aces((hdr + bloom) * exposure);
    fragment = vec4(srgbEncode(tonemapped), 1.0);
}
)";

}  // namespace grace::render::opengl
