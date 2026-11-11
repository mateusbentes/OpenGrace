#pragma once

// The Vulkan backend's shader library. RULE (decision 14, the same as
// the OpenGL backend's): every engine and test shader source lives
// ONLY in this file, under src/render/vulkan/. The API-neutral render
// module never contains shading-language source; programs are
// addressed through the API-neutral ShaderProgramId enum and
// resolved here. Adding a shader = one enum entry + one entry here.
//
// The Vulkan shaders are GLSL 450 core with one std140 uniform block
// (set=0, binding=0) and samplers on sequential bindings (binding 1,
// 2, ... in declaration order). The engine sets the uniforms through
// the API-neutral ShaderProgram::setUniform contract; the std140
// layout table is what the backend writes to the uniform buffer.
//
// The conversion is mechanical: every non-sampler uniform becomes a
// member of the block, the body is unchanged (block members are
// accessed by name exactly like loose uniforms), and the vertex
// inputs and the fragment outputs get explicit layout(location)
// values in declaration order.

namespace grace::render::vulkan {

// The per-shader std140 layout table, for the backend to write the
// uniform buffer. One member's name, type, offset and size (std140).
struct ShaderBlockLayout
{
    struct Member
    {
        const char* name;
        const char* type;
        std::uint32_t offset;
        std::uint32_t size;
    };

    const Member* members;
    std::uint32_t memberCount;
};

// One shader's layout entry: the shader literal's name plus its
// member table.
struct ShaderSampler
{
    const char* name;
    std::uint32_t binding;
    // The GLSL declaration's descriptor count: the engine's sampler2D
    // arrays (shadowMaps[4], pointShadowFront[2], pointShadowBack[2])
    // declare an array of samplers, one descriptor per element; the scalar
    // samplers (gbufAlbedo, volumeTex, srcTex, ...) carry one.
    std::uint32_t count;
    // The engine's texture-unit rule for the entry's elements: the engine
    // binds the array element e at unit = unitBase + unitStride * e (the
    // same units the OpenGL backend's glUniform1i contract uses), the
    // scalar samplers at unitBase alone. The engine's unit rule is the
    // GL declaration's one, the same rule the OpenGL contract follows.
    std::uint32_t unitBase;
    std::uint32_t unitStride;
};

struct ShaderLayoutEntry
{
    const char* shader;
    const ShaderBlockLayout::Member* members;
    std::uint32_t memberCount;
    const ShaderSampler* samplers;
    std::uint32_t samplerCount;
};

// Layout lookup by shader literal name. The backend indexes this
// table with the same order the ShaderProgramId enum resolves, so a
// program's uniform writes land at the std140 offsets the SPIR-V
// block declares.
// kTriangleVertexShader
//   offset   0 size 64 mat4 mvp
constexpr const char* kTriangleVertexShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 mvp;
};
layout(location = 0) in vec3 position;
void main()
{
    gl_Position = mvp * vec4(position, 1.0);
}
)";

// kTriangleFragmentShader
//   offset   0 size 16 vec4 color
constexpr const char* kTriangleFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec4 color;
};
layout(location = 0) out vec4 fragment;
void main()
{
    fragment = color;
}
)";

// kLightingVertexShader
//   offset   0 size 64 mat4 mvp
//   offset  64 size 64 mat4 model
//   offset 128 size 64 mat4 viewMatrix
constexpr const char* kLightingVertexShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 mvp;
    mat4 model;
    mat4 viewMatrix;
};
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out float vViewDepth;
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

// kLightingFragmentShader
//   offset   0 size 12 vec3 cameraPos
//   offset  16 size 12 vec3 albedo
//   offset  32 size 12 vec3 specular
//   offset  44 size  4 float shininess
//   offset  48 size 12 vec3 emissive
//   offset  60 size  4 float ambient
//   offset  64 size  4 int hasSun
//   offset  80 size 12 vec3 sunDirection
//   offset  96 size 12 vec3 sunColor
//   offset 108 size  4 float sunIntensity
//   offset 112 size  4 int lightCount
//   offset 128 size 64 int lightType[4]
//   offset 192 size 64 vec3 lightPos[4]
//   offset 256 size 64 vec3 lightDir[4]
//   offset 320 size 64 vec3 lightColor[4]
//   offset 384 size 64 float lightIntensity[4]
//   offset 448 size 64 float lightRange[4]
//   offset 512 size 64 float lightInner[4]
//   offset 576 size 64 float lightOuter[4]
//   offset 640 size  4 int hasShadowMap
//   offset 644 size  4 int cascadeCount
//   offset 704 size 256 mat4 lightMatrices[4]
//   offset 960 size 80 float splitDepths[5]
//   offset 1040 size  4 float shadowBias
//   offset 1044 size  4 float filterTolerance
//   offset 1048 size  4 float shadowMapSize
//   offset 1052 size  4 int debugCascades
//   offset 1056 size  4 int pointShadowCount
//   offset 1088 size 128 mat4 pointLightViews[2]
//   offset 1216 size 32 vec3 pointLightPos[2]
//   offset 1248 size 32 vec2 pointLightRange[2]
//   offset 1280 size 32 float pointShadowBias[2]
//   offset 1312 size  4 float pointFilterTolerance
//   offset 1316 size  4 float pointShadowMapSize
//   offset 1328 size 64 int pointLightSlot[4]
//   offset 1392 size  4 int hasVolume
//   offset 1408 size 12 vec3 volumeOrigin
//   offset 1424 size 12 vec3 volumeExtent
constexpr const char* kLightingFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec3 cameraPos;
    vec3 albedo;
    vec3 specular;
    float shininess;
    vec3 emissive;
    float ambient;
    int hasSun;
    vec3 sunDirection;
    vec3 sunColor;
    float sunIntensity;
    int lightCount;
    int lightType[4];
    vec3 lightPos[4];
    vec3 lightDir[4];
    vec3 lightColor[4];
    float lightIntensity[4];
    float lightRange[4];
    float lightInner[4];
    float lightOuter[4];
    int hasShadowMap;
    int cascadeCount;
    mat4 lightMatrices[4];
    float splitDepths[5];
    float shadowBias;
    float filterTolerance;
    float shadowMapSize;
    int debugCascades;
    int pointShadowCount;
    mat4 pointLightViews[2];
    vec3 pointLightPos[2];
    vec2 pointLightRange[2];
    float pointShadowBias[2];
    float pointFilterTolerance;
    float pointShadowMapSize;
    int pointLightSlot[4];
    int hasVolume;
    vec3 volumeOrigin;
    vec3 volumeExtent;
};
layout(set = 0, binding = 1) uniform sampler2D shadowMaps[4];
layout(set = 0, binding = 2) uniform sampler2D pointShadowFront[2];
layout(set = 0, binding = 3) uniform sampler2D pointShadowBack[2];
layout(set = 0, binding = 4) uniform sampler3D volumeTex;
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in float vViewDepth;
layout(location = 1) out vec4 outAmbient;
// Cascaded sun shadow map (milestone 4c), sun term only.
// Dual paraboloid point-light shadow maps (milestone 4d). pointLightSlot[i]
// associates dynamic light i with a map entry (or -1 = no shadow map: the
// light is applied fully, documented). Front/back are the two hemispheres.

// Light volume compose (milestone 7, fourth increment): the volume, sampled
// at the fragment's world position, adds albedo * radiance to the ambient
// term (decision 17(d)). hasVolume = 0 keeps the frame unchanged (the compose
// flag is off by default).

layout(location = 3) out vec4 fragment;

float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}

// One cascade's shadow factor at the fragment, with the bilateral filter.
// uv/depth are this cascade's light-space projection of the fragment.
// GLSL 450 allows dynamic indexing of sampler arrays, so one function covers
// the whole cascade array.
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
// not clip depth. GLSL 450 allows dynamic indexing of sampler arrays, so one
// function covers both hemispheres.
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
    // Light volume compose (decision 17(d)): albedo * radiance, sampled at the
    // fragment's world position. The volume covers [origin, origin+extent];
    // a fragment outside it is unchanged.
    vec3 volumeRadiance = vec3(0.0);
    if (hasVolume == 1)
    {
        vec3 vc = (vWorldPos - volumeOrigin) / volumeExtent;
        if (all(greaterThanEqual(vc, vec3(-0.001))) &&
            all(lessThanEqual(vc, vec3(1.001))))
            volumeRadiance = texture(volumeTex, vc).rgb;
    }
    vec3 ambientTerm = vec3(ambient) + volumeRadiance;

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

    vec3 result = emissive + albedo * ambientTerm;
    outAmbient = vec4(albedo * ambientTerm, 1.0);
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

// kShadowDepthVertexShader
//   offset   0 size 64 mat4 lightMVP
constexpr const char* kShadowDepthVertexShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 lightMVP;
};
layout(location = 0) in vec3 position;
void main()
{
    gl_Position = lightMVP * vec4(position, 1.0);
}
)";

// kShadowDepthFragmentShader
constexpr const char* kShadowDepthFragmentShader = R"(
#version 450 core
void main()
{
}
)";

// kParaboloidDepthVertexShader
//   offset   0 size 64 mat4 lightView
//   offset  64 size 64 mat4 model
//   offset 128 size  4 float nearRange
//   offset 132 size  4 float farRange
//   offset 136 size  4 int hemisphere
constexpr const char* kParaboloidDepthVertexShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 lightView;
    mat4 model;
    float nearRange;
    float farRange;
    int hemisphere;
};
layout(location = 0) in vec3 position;
layout(location = 0) out vec3 vL;
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

// kParaboloidDepthFragmentShader
//   offset   0 size  4 float nearRange
//   offset   4 size  4 float farRange
constexpr const char* kParaboloidDepthFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    float nearRange;
    float farRange;
};
layout(location = 0) in vec3 vL;
void main()
{
    float dist = length(vL);
    gl_FragDepth = clamp((dist - nearRange) / (farRange - nearRange), 0.0, 1.0);
}
)";

// kGBufferVertexShader
//   offset   0 size 64 mat4 mvp
//   offset  64 size 64 mat4 model
constexpr const char* kGBufferVertexShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 mvp;
    mat4 model;
};
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
void main()
{
    vec4 wp = model * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(model) * normal;
    gl_Position = mvp * vec4(position, 1.0);
}
)";

// kGBufferFragmentShader
//   offset   0 size 12 vec3 albedo
//   offset  16 size 12 vec3 specular
//   offset  28 size  4 float shininess
constexpr const char* kGBufferFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec3 albedo;
    vec3 specular;
    float shininess;
};
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
void main()
{
    outAlbedo = vec4(albedo, shininess / 1024.0);  // G-buffer, unchanged
    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5,
                     dot(specular, vec3(0.299, 0.587, 0.114)));
}
)";

// kDeferredLightVertexShader
constexpr const char* kDeferredLightVertexShader = R"(
#version 450 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

// kDeferredLightFragmentShader
//   offset   0 size 64 mat4 invProj
//   offset  64 size 64 mat4 invView
//   offset 128 size 12 vec3 cameraPos
//   offset 144 size 16 vec4 clearColor
//   offset 160 size  4 float ambient
//   offset 164 size  4 int hasSun
//   offset 176 size 12 vec3 sunDirection
//   offset 192 size 12 vec3 sunColor
//   offset 204 size  4 float sunIntensity
//   offset 208 size  4 int lightCount
//   offset 224 size 256 int lightType[16]
//   offset 480 size  4 int hasShadowMap
//   offset 484 size  4 int cascadeCount
//   offset 512 size 256 mat4 lightMatrices[4]
//   offset 768 size 80 float splitDepths[5]
//   offset 848 size  4 float shadowBias
//   offset 852 size  4 float filterTolerance
//   offset 856 size  4 float shadowMapSize
//   offset 860 size  4 int pointShadowCount
//   offset 896 size 128 mat4 pointLightViews[2]
//   offset 1024 size 32 vec3 pointLightPos[2]
//   offset 1056 size 32 vec2 pointLightRange[2]
//   offset 1088 size 32 float pointShadowBias[2]
//   offset 1120 size  4 float pointFilterTolerance
//   offset 1124 size  4 float pointShadowMapSize
//   offset 1136 size 256 int pointLightSlot[16]
//   offset 1392 size 256 vec3 lightPos[16]
//   offset 1648 size 256 vec3 lightDir[16]
//   offset 1904 size 256 vec3 lightColor[16]
//   offset 2160 size 256 float lightIntensity[16]
//   offset 2416 size 256 float lightRange[16]
//   offset 2672 size 256 float lightInner[16]
//   offset 2928 size 256 float lightOuter[16]
//   offset 3184 size  4 int hasVolume
//   offset 3200 size 12 vec3 volumeOrigin
//   offset 3216 size 12 vec3 volumeExtent
constexpr const char* kDeferredLightFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 invProj;
    mat4 invView;
    vec3 cameraPos;
    vec4 clearColor;
    float ambient;
    int hasSun;
    vec3 sunDirection;
    vec3 sunColor;
    float sunIntensity;
    int lightCount;
    int lightType[16];
    int hasShadowMap;
    int cascadeCount;
    mat4 lightMatrices[4];
    float splitDepths[5];
    float shadowBias;
    float filterTolerance;
    float shadowMapSize;
    int pointShadowCount;
    mat4 pointLightViews[2];
    vec3 pointLightPos[2];
    vec2 pointLightRange[2];
    float pointShadowBias[2];
    float pointFilterTolerance;
    float pointShadowMapSize;
    int pointLightSlot[16];
    vec3 lightPos[16];
    vec3 lightDir[16];
    vec3 lightColor[16];
    float lightIntensity[16];
    float lightRange[16];
    float lightInner[16];
    float lightOuter[16];
    int hasVolume;
    vec3 volumeOrigin;
    vec3 volumeExtent;
};
layout(set = 0, binding = 1) uniform sampler2D gbufAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gbufNormal;
layout(set = 0, binding = 3) uniform sampler2D gbufDepth;
layout(set = 0, binding = 4) uniform sampler2D shadowMaps[4];
layout(set = 0, binding = 5) uniform sampler2D pointShadowFront[2];
layout(set = 0, binding = 6) uniform sampler2D pointShadowBack[2];
layout(set = 0, binding = 7) uniform sampler3D volumeTex;
layout(location = 1) out vec4 outAmbient;
// Sun cascade shadow map (consumed here since 5c) — same uniforms as forward.
// Paraboloid point-light shadow maps (consumed since 5c).

// Light volume compose (milestone 7, fourth increment): the volume, sampled
// at the fragment's world position, adds albedo * radiance to the ambient
// term (decision 17(d)). hasVolume = 0 keeps the frame unchanged.

layout(location = 0) out vec4 fragment;

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

    // Light volume compose (decision 17(d)): albedo * radiance, sampled at the
    // fragment's world position. The volume covers [origin, origin+extent];
    // a fragment outside it is unchanged.
    vec3 volumeRadiance = vec3(0.0);
    if (hasVolume == 1)
    {
        vec3 vc = (world.xyz - volumeOrigin) / volumeExtent;
        if (all(greaterThanEqual(vc, vec3(-0.001))) &&
            all(lessThanEqual(vc, vec3(1.001))))
            volumeRadiance = texture(volumeTex, vc).rgb;
    }
    vec3 ambientTerm = vec3(ambient) + volumeRadiance;

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

    vec3 result = albedo * ambientTerm;
    outAmbient = vec4(albedo * ambientTerm, 1.0);
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

// kFlatVertexShader
constexpr const char* kFlatVertexShader = R"(
#version 450 core
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(position, 1.0); }
)";

// kFlatFragmentShader
//   offset   0 size 16 vec4 color
constexpr const char* kFlatFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec4 color;
};
layout(location = 0) out vec4 fragment;
void main() { fragment = color; }
)";

// kBrokenVertexShader
constexpr const char* kBrokenVertexShader = R"(
#version 450 core
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(broken_identifier_xyz, 1.0); }
)";

// kBrokenFragmentShader
constexpr const char* kBrokenFragmentShader = R"(
#version 450 core
layout(location = 0) out vec4 fragment;
void main() { fragment = color_uniform_that_does_not_exist; }
)";

// kPostBrightFragmentShader
//   offset   0 size  8 vec2 outSize
//   offset   8 size  4 float threshold
constexpr const char* kPostBrightFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec2 outSize;
    float threshold;
};
layout(set = 0, binding = 1) uniform sampler2D srcTex;
layout(location = 0) out vec4 fragment;
void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec3 c = texture(srcTex, uv).rgb;
    fragment = vec4(max(c - vec3(threshold), vec3(0.0)), 1.0);
}
)";

// kPostBlurFragmentShader
//   offset   0 size  8 vec2 outSize
//   offset   8 size  8 vec2 direction
constexpr const char* kPostBlurFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec2 outSize;
    vec2 direction;
};
layout(set = 0, binding = 1) uniform sampler2D srcTex;
layout(location = 0) out vec4 fragment;
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

// kPostTonemapFragmentShader
//   offset   0 size  4 int useSSAO
//   offset   4 size  4 int useVolumetric
//   offset   8 size  8 vec2 outSize
//   offset  16 size  4 float exposure
constexpr const char* kPostTonemapFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    int useSSAO;
    int useVolumetric;
    vec2 outSize;
    float exposure;
};
layout(set = 0, binding = 1) uniform sampler2D hdrTex;
layout(set = 0, binding = 2) uniform sampler2D hdrAmbient;
layout(set = 0, binding = 3) uniform sampler2D bloomTex;
layout(set = 0, binding = 4) uniform sampler2D ssaoTex;
layout(set = 0, binding = 5) uniform sampler2D volTex;
layout(location = 0) out vec4 fragment;

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
    // 6f: volumetric in-scattering is added in LINEAR space, before the
    // tonemap, so it responds to exposure like all other lighting.
    vec3 vol = useVolumetric == 1 ? texture(volTex, uv).rgb : vec3(0.0);
    // 6c: occlusion multiplies the ambient term only: direct + ambient * occ.
    vec3 tonemapped = aces((hdr - amb + amb * occ + bloom + vol) * exposure);
    fragment = vec4(srgbEncode(tonemapped), 1.0);
}
)";

// kPostSSAOFragmentShader
//   offset   0 size 64 mat4 invProj
//   offset  64 size 64 mat4 proj
//   offset 128 size  8 vec2 outSize
//   offset 136 size  4 float radius
//   offset 140 size  4 int sampleCount
//   offset 144 size  4 float bias
constexpr const char* kPostSSAOFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 invProj;
    mat4 proj;
    vec2 outSize;
    float radius;
    int sampleCount;
    float bias;
};
layout(set = 0, binding = 1) uniform sampler2D depthTex;
layout(location = 0) out vec4 fragment;

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

// kPostSSAOStfFragmentShader
//   offset   0 size 64 mat4 invProj
//   offset  64 size 64 mat4 invView
//   offset 128 size 64 mat4 prevViewProj
//   offset 192 size  8 vec2 outSize
//   offset 200 size  4 float blend
//   offset 204 size  4 float depthRejection
//   offset 208 size  4 int hasHistory
constexpr const char* kPostSSAOStfFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 invProj;
    mat4 invView;
    mat4 prevViewProj;
    vec2 outSize;
    float blend;
    float depthRejection;
    int hasHistory;
};
layout(set = 0, binding = 1) uniform sampler2D depthTex;
layout(set = 0, binding = 2) uniform sampler2D ssaoRawTex;
layout(set = 0, binding = 3) uniform sampler2D historyTex;
layout(set = 0, binding = 4) uniform sampler2D historyDepthTex;
layout(location = 0) out float filteredOcc;
layout(location = 1) out float historyDepth;
                                             // validated by the next frame's pass

vec3 viewPos(vec2 uv, float d)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = invProj * clip;
    return v.xyz / v.w;
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    float raw = texture(ssaoRawTex, uv).r;
    // Cold start: the first frame has no history, so the output is the raw
    // buffer. The history target is written either way, so the next frame's
    // cold start disappears.
    if (hasHistory == 0)
    {
        // The cold start still writes the current depth, so the next frame's
        // validation has a history to validate against.
        float coldDepth = texture(depthTex, uv).r;
        filteredOcc = raw;
        historyDepth = coldDepth;
        return;
    }
    float d = texture(depthTex, uv).r;
    if (d >= 1.0)
    {
        // Background: the raw buffer is 1.0 and there is no history to
        // validate — the raw value is the output.
        filteredOcc = raw;
        historyDepth = d;
        return;
    }
    // Reproject: current pixel's world position, projected with the previous
    // frame's view-projection matrix.
    vec3 view = viewPos(uv, d);
    vec4 world = invView * vec4(view, 1.0);
    vec4 prevClip = prevViewProj * world;
    if (prevClip.w <= 0.0)
    {
        // Behind the previous eye: the history is invalid.
        filteredOcc = raw;
        historyDepth = d;
        return;
    }
    vec2 prevUv = prevClip.xy / prevClip.w * 0.5 + 0.5;
    if (prevUv.x < 0.0 || prevUv.x > 1.0 || prevUv.y < 0.0 || prevUv.y > 1.0)
    {
        // Off-screen in the previous frame: the history is invalid.
        filteredOcc = raw;
        historyDepth = d;
        return;
    }
    float histOcc = texture(historyTex, prevUv).r;
    float histDepth = texture(historyDepthTex, prevUv).r;
    if (abs(histDepth - d) > depthRejection)
    {
        // Disoccluded or a different surface: the history is invalid.
        filteredOcc = raw;
        historyDepth = d;
        return;
    }
    filteredOcc = mix(raw, histOcc, blend);
    historyDepth = d;
}
)";

// kPostMLAAFragmentShader
//   offset   0 size  8 vec2 outSize
//   offset   8 size  4 float lumThreshold
//   offset  12 size  4 float depthThreshold
constexpr const char* kPostMLAAFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec2 outSize;
    float lumThreshold;
    float depthThreshold;
};
layout(set = 0, binding = 1) uniform sampler2D srcTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;
layout(location = 0) out vec4 fragment;

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

// kPostBlitFragmentShader
constexpr const char* kPostBlitFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 1) uniform sampler2D srcTex;
layout(location = 0) out vec4 fragment;
void main()
{
    fragment = texture(srcTex, gl_FragCoord.xy / vec2(textureSize(srcTex, 0)));
}
)";

// kPostDOFFragmentShader
//   offset   0 size 64 mat4 invProj
//   offset  64 size  8 vec2 outSize
//   offset  72 size  4 float tanHalfFovY
//   offset  76 size  4 float focusDistance
//   offset  80 size  4 float aperture
//   offset  84 size  4 float maxCocPixels
constexpr const char* kPostDOFFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 invProj;
    vec2 outSize;
    float tanHalfFovY;
    float focusDistance;
    float aperture;
    float maxCocPixels;
};
layout(set = 0, binding = 1) uniform sampler2D srcTex;
layout(set = 0, binding = 2) uniform sampler2D srcAmbient;
layout(set = 0, binding = 3) uniform sampler2D depthTex;
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
    // Circle of confusion (documented formula and clamp, decision 22): the
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

// kPostVolumetricFragmentShader
//   offset   0 size 64 mat4 invProj
//   offset  64 size 64 mat4 invView
//   offset 128 size 12 vec3 cameraPos
//   offset 144 size  8 vec2 outSize
//   offset 152 size  4 int volCount
//   offset 160 size 64 vec3 volPos[4]
//   offset 224 size 64 vec3 volDir[4]
//   offset 288 size 64 vec3 volColor[4]
//   offset 352 size 64 float volIntensity[4]
//   offset 416 size 64 float volRadius[4]
//   offset 480 size 64 float volInnerCos[4]
//   offset 544 size 64 float volOuterCos[4]
//   offset 608 size 64 int volIsCone[4]
//   offset 672 size 64 float volDensity[4]
//   offset 736 size 64 int volShadowSlot[4]
//   offset 832 size 128 mat4 pointLightViews[2]
//   offset 960 size 32 vec2 pointLightRange[2]
//   offset 992 size 32 float pointShadowBias[2]
constexpr const char* kPostVolumetricFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 invProj;
    mat4 invView;
    vec3 cameraPos;
    vec2 outSize;
    int volCount;
    vec3 volPos[4];
    vec3 volDir[4];
    vec3 volColor[4];
    float volIntensity[4];
    float volRadius[4];
    float volInnerCos[4];
    float volOuterCos[4];
    int volIsCone[4];
    float volDensity[4];
    int volShadowSlot[4];
    mat4 pointLightViews[2];
    vec2 pointLightRange[2];
    float pointShadowBias[2];
};
layout(set = 0, binding = 1) uniform sampler2D depthTex;
layout(set = 0, binding = 2) uniform sampler2D pointShadowFront[2];
layout(set = 0, binding = 3) uniform sampler2D pointShadowBack[2];
layout(location = 0) out vec4 fragment;

vec3 viewPos(vec2 uv, float d)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = invProj * clip;
    return v.xyz / v.w;
}

// Documented windowed inverse-square range attenuation (same as surfaces).
float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}

// Single-tap paraboloid lookup (volume pass; the bilateral 3x3 filter is a
// surface-shading property). Outside the map or beyond far: lit (documented).
float tapF0(vec2 uv, float d, float b) { float s = texture(pointShadowFront[0], uv).r; return (d <= s + b) ? 1.0 : 0.0; }
float tapB0(vec2 uv, float d, float b) { float s = texture(pointShadowBack[0], uv).r; return (d <= s + b) ? 1.0 : 0.0; }
float tapF1(vec2 uv, float d, float b) { float s = texture(pointShadowFront[1], uv).r; return (d <= s + b) ? 1.0 : 0.0; }
float tapB1(vec2 uv, float d, float b) { float s = texture(pointShadowBack[1], uv).r; return (d <= s + b) ? 1.0 : 0.0; }

float volShadow(int slot, vec3 wp)
{
    if (slot < 0) return 1.0;
    vec4 l = pointLightViews[slot] * vec4(wp, 1.0);
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
    if (luv.x < 0.0 || luv.x > 1.0 || luv.y < 0.0 || luv.y > 1.0 || ldepth > 1.0) return 1.0;
    if (slot <= 0)
        return hemi == 0 ? tapF0(luv, ldepth, pointShadowBias[0]) : tapB0(luv, ldepth, pointShadowBias[0]);
    return hemi == 0 ? tapF1(luv, ldepth, pointShadowBias[1]) : tapB1(luv, ldepth, pointShadowBias[1]);
}

// Ray-sphere: returns (t0, t1); both negative when there is no hit ahead.
vec2 raySphere(vec3 o, vec3 d, vec3 c, float r)
{
    vec3 oc = o - c;
    float b = dot(oc, d);
    float cc = dot(oc, oc) - r * r;
    float disc = b * b - cc;
    if (disc < 0.0) return vec2(-1.0, -1.0);
    float s = sqrt(disc);
    return vec2(-b - s, -b + s);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    float d = texture(depthTex, uv).r;
    vec3 sp = viewPos(uv, d);          // view-space surface point (far plane if background)
    float rayDist = length(sp);        // camera sits at the view origin
    vec4 pv = invProj * vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    vec3 dirView = normalize(pv.xyz / pv.w);
    vec4 dw = invView * vec4(dirView, 0.0);
    vec3 dir = normalize(dw.xyz);
    vec3 o = cameraPos;

    vec3 inscatter = vec3(0.0);
    for (int i = 0; i < volCount && i < 4; ++i)
    {
        float t0 = 0.0;
        float t1 = rayDist;
        if (volIsCone[i] == 0)
        {
            vec2 tr = raySphere(o, dir, volPos[i], volRadius[i]);
            if (tr.y <= 0.0) continue;          // sphere behind the camera
            t0 = max(tr.x, 0.0);                // camera inside: clamp entry to 0
            t1 = min(tr.y, rayDist);
        }
        else
        {
            vec3 axis = normalize(volDir[i]);
            float dA = dot(dir, axis);
            if (dA <= 1e-4) continue;           // ray never enters the forward cone
            t1 = min(rayDist, volRadius[i] / dA);  // cone truncated by range
        }
        if (t1 <= t0) continue;
        float ds = (t1 - t0) / 16.0;            // 16 fixed samples along the ray
        float sum = 0.0;
        for (int s = 0; s < 16; ++s)
        {
            float t = t0 + (float(s) + 0.5) * ds;
            vec3 p = o + dir * t;
            vec3 toL = volPos[i] - p;
            float dist = length(toL);
            vec3 L = toL / max(dist, 1e-6);
            float atten = rangeAtten(volIntensity[i], volRadius[i], dist);
            if (volIsCone[i] == 1)
            {
                float cone = smoothstep(volOuterCos[i], volInnerCos[i],
                                        dot(-L, normalize(volDir[i])));
                if (cone <= 0.0) continue;
                atten *= cone;
            }
            else if (dist > volRadius[i])
                continue;
            float shadow = volShadow(volShadowSlot[i], p);
            sum += atten * shadow * ds;
        }
        inscatter += volColor[i] * volDensity[i] * sum / (4.0 * 3.14159265358979);
    }
    fragment = vec4(inscatter, 1.0);
}
)";

// kVolumeInjectFragmentShader
//   offset   0 size 12 vec3 volumeOrigin
//   offset  16 size 12 vec3 volumeExtent
//   offset  32 size  8 vec2 outSize
//   offset  40 size  4 float layerCoord
//   offset  44 size  4 int lightCount
//   offset  48 size 64 int lightType[4]
//   offset 112 size 64 vec3 lightPos[4]
//   offset 176 size 64 vec3 lightDir[4]
//   offset 240 size 64 vec3 lightColor[4]
//   offset 304 size 64 float lightIntensity[4]
//   offset 368 size 64 float lightRange[4]
//   offset 432 size 64 float lightInner[4]
//   offset 496 size 64 float lightOuter[4]
constexpr const char* kVolumeInjectFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec3 volumeOrigin;
    vec3 volumeExtent;
    vec2 outSize;
    float layerCoord;
    int lightCount;
    int lightType[4];
    vec3 lightPos[4];
    vec3 lightDir[4];
    vec3 lightColor[4];
    float lightIntensity[4];
    float lightRange[4];
    float lightInner[4];
    float lightOuter[4];
};
layout(location = 0) out vec4 fragment;

// The same windowed inverse-square attenuation the surface shader uses, so the
// volume cannot be lit differently from the surfaces around it.
float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec3 p = volumeOrigin + vec3(uv.x, uv.y, layerCoord) * volumeExtent;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < lightCount && i < 4; ++i)
    {
        vec3 toLight = lightPos[i] - p;
        float d = length(toLight);
        vec3 L = toLight / max(d, 1e-6);
        float cone = 1.0;
        if (lightType[i] == 1)
        {
            float cosAngle = dot(-L, normalize(lightDir[i]));
            float cosInner = cos(lightInner[i]);
            float cosOuter = cos(lightOuter[i]);
            float t = clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0.0, 1.0);
            cone = t * t * (3.0 - 2.0 * t);
        }
        sum += lightColor[i] * rangeAtten(lightIntensity[i], lightRange[i], d) * cone;
    }
    fragment = vec4(sum, 1.0);  // linear HDR, like the lighting target
}
)";

// kVolumePropagateFragmentShader
//   offset   0 size 12 vec3 texelSize
//   offset  16 size  8 vec2 outSize
//   offset  24 size  4 float layerCoord
constexpr const char* kVolumePropagateFragmentShader = R"(
#version 450 core
layout(set = 0, binding = 0, std140) uniform Block
{
    vec3 texelSize;
    vec2 outSize;
    float layerCoord;
};
layout(set = 0, binding = 1) uniform sampler3D volumeIn;
layout(location = 0) out vec4 fragment;
void main()
{
    vec2 uv = gl_FragCoord.xy / outSize;
    vec3 coord = vec3(uv.x, uv.y, layerCoord);
    vec3 sum = vec3(0.0);
    sum += texture(volumeIn, coord + vec3(texelSize.x, 0.0, 0.0)).rgb;
    sum += texture(volumeIn, coord - vec3(texelSize.x, 0.0, 0.0)).rgb;
    sum += texture(volumeIn, coord + vec3(0.0, texelSize.y, 0.0)).rgb;
    sum += texture(volumeIn, coord - vec3(0.0, texelSize.y, 0.0)).rgb;
    sum += texture(volumeIn, coord + vec3(0.0, 0.0, texelSize.z)).rgb;
    sum += texture(volumeIn, coord - vec3(0.0, 0.0, texelSize.z)).rgb;
    fragment = vec4(sum / 6.0, 1.0);  // linear HDR, like the lighting target
}
)";

// The per-shader member tables. Sorted by shader name so the
// declaration order is stable and reviewable.
constexpr ShaderBlockLayout::Member kTriangleVertexShaderMembers[] = {
    {"mvp", "mat4", 0, 64},
};

constexpr ShaderSampler kTriangleVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kTriangleFragmentShaderMembers[] = {
    {"color", "vec4", 0, 16},
};

constexpr ShaderSampler kTriangleFragmentShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kLightingVertexShaderMembers[] = {
    {"mvp", "mat4", 0, 64},
    {"model", "mat4", 64, 64},
    {"viewMatrix", "mat4", 128, 64},
};

constexpr ShaderSampler kLightingVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kLightingFragmentShaderMembers[] = {
    {"cameraPos", "vec3", 0, 12},
    {"albedo", "vec3", 16, 12},
    {"specular", "vec3", 32, 12},
    {"shininess", "float", 44, 4},
    {"emissive", "vec3", 48, 12},
    {"ambient", "float", 60, 4},
    {"hasSun", "int", 64, 4},
    {"sunDirection", "vec3", 80, 12},
    {"sunColor", "vec3", 96, 12},
    {"sunIntensity", "float", 108, 4},
    {"lightCount", "int", 112, 4},
    {"lightType", "int", 128, 64},
    {"lightPos", "vec3", 192, 64},
    {"lightDir", "vec3", 256, 64},
    {"lightColor", "vec3", 320, 64},
    {"lightIntensity", "float", 384, 64},
    {"lightRange", "float", 448, 64},
    {"lightInner", "float", 512, 64},
    {"lightOuter", "float", 576, 64},
    {"hasShadowMap", "int", 640, 4},
    {"cascadeCount", "int", 644, 4},
    {"lightMatrices", "mat4", 704, 256},
    {"splitDepths", "float", 960, 80},
    {"shadowBias", "float", 1040, 4},
    {"filterTolerance", "float", 1044, 4},
    {"shadowMapSize", "float", 1048, 4},
    {"debugCascades", "int", 1052, 4},
    {"pointShadowCount", "int", 1056, 4},
    {"pointLightViews", "mat4", 1088, 128},
    {"pointLightPos", "vec3", 1216, 32},
    {"pointLightRange", "vec2", 1248, 32},
    {"pointShadowBias", "float", 1280, 32},
    {"pointFilterTolerance", "float", 1312, 4},
    {"pointShadowMapSize", "float", 1316, 4},
    {"pointLightSlot", "int", 1328, 64},
    {"hasVolume", "int", 1392, 4},
    {"volumeOrigin", "vec3", 1408, 12},
    {"volumeExtent", "vec3", 1424, 12},
};

constexpr ShaderSampler kLightingFragmentShaderSamplers[] = {
    {"shadowMaps", 1, 4, 0, 1},
    {"pointShadowFront", 2, 2, 4, 2},
    {"pointShadowBack", 3, 2, 5, 2},
    {"volumeTex", 4, 1, 9, 1},
};

constexpr ShaderBlockLayout::Member kShadowDepthVertexShaderMembers[] = {
    {"lightMVP", "mat4", 0, 64},
};

constexpr ShaderSampler kShadowDepthVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kShadowDepthFragmentShaderMembers[] = {
    {nullptr, nullptr, 0, 0},
};

constexpr ShaderSampler kShadowDepthFragmentShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kParaboloidDepthVertexShaderMembers[] = {
    {"lightView", "mat4", 0, 64},
    {"model", "mat4", 64, 64},
    {"nearRange", "float", 128, 4},
    {"farRange", "float", 132, 4},
    {"hemisphere", "int", 136, 4},
};

constexpr ShaderSampler kParaboloidDepthVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kParaboloidDepthFragmentShaderMembers[] = {
    {"nearRange", "float", 0, 4},
    {"farRange", "float", 4, 4},
};

constexpr ShaderSampler kParaboloidDepthFragmentShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kGBufferVertexShaderMembers[] = {
    {"mvp", "mat4", 0, 64},
    {"model", "mat4", 64, 64},
};

constexpr ShaderSampler kGBufferVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kGBufferFragmentShaderMembers[] = {
    {"albedo", "vec3", 0, 12},
    {"specular", "vec3", 16, 12},
    {"shininess", "float", 28, 4},
};

constexpr ShaderSampler kGBufferFragmentShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kDeferredLightVertexShaderMembers[] = {
    {nullptr, nullptr, 0, 0},
};

constexpr ShaderSampler kDeferredLightVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kDeferredLightFragmentShaderMembers[] = {
    {"invProj", "mat4", 0, 64},
    {"invView", "mat4", 64, 64},
    {"cameraPos", "vec3", 128, 12},
    {"clearColor", "vec4", 144, 16},
    {"ambient", "float", 160, 4},
    {"hasSun", "int", 164, 4},
    {"sunDirection", "vec3", 176, 12},
    {"sunColor", "vec3", 192, 12},
    {"sunIntensity", "float", 204, 4},
    {"lightCount", "int", 208, 4},
    {"lightType", "int", 224, 256},
    {"hasShadowMap", "int", 480, 4},
    {"cascadeCount", "int", 484, 4},
    {"lightMatrices", "mat4", 512, 256},
    {"splitDepths", "float", 768, 80},
    {"shadowBias", "float", 848, 4},
    {"filterTolerance", "float", 852, 4},
    {"shadowMapSize", "float", 856, 4},
    {"pointShadowCount", "int", 860, 4},
    {"pointLightViews", "mat4", 896, 128},
    {"pointLightPos", "vec3", 1024, 32},
    {"pointLightRange", "vec2", 1056, 32},
    {"pointShadowBias", "float", 1088, 32},
    {"pointFilterTolerance", "float", 1120, 4},
    {"pointShadowMapSize", "float", 1124, 4},
    {"pointLightSlot", "int", 1136, 256},
    {"lightPos", "vec3", 1392, 256},
    {"lightDir", "vec3", 1648, 256},
    {"lightColor", "vec3", 1904, 256},
    {"lightIntensity", "float", 2160, 256},
    {"lightRange", "float", 2416, 256},
    {"lightInner", "float", 2672, 256},
    {"lightOuter", "float", 2928, 256},
    {"hasVolume", "int", 3184, 4},
    {"volumeOrigin", "vec3", 3200, 12},
    {"volumeExtent", "vec3", 3216, 12},
};

constexpr ShaderSampler kDeferredLightFragmentShaderSamplers[] = {
    {"gbufAlbedo", 1, 1, 0, 1},
    {"gbufNormal", 2, 1, 1, 1},
    {"gbufDepth", 3, 1, 2, 1},
    {"shadowMaps", 4, 4, 0, 1},
    {"pointShadowFront", 5, 2, 4, 2},
    {"pointShadowBack", 6, 2, 5, 2},
    {"volumeTex", 7, 1, 9, 1},
};

constexpr ShaderBlockLayout::Member kFlatVertexShaderMembers[] = {
    {nullptr, nullptr, 0, 0},
};

constexpr ShaderSampler kFlatVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kFlatFragmentShaderMembers[] = {
    {"color", "vec4", 0, 16},
};

constexpr ShaderSampler kFlatFragmentShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kBrokenVertexShaderMembers[] = {
    {nullptr, nullptr, 0, 0},
};

constexpr ShaderSampler kBrokenVertexShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kBrokenFragmentShaderMembers[] = {
    {nullptr, nullptr, 0, 0},
};

constexpr ShaderSampler kBrokenFragmentShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kPostBrightFragmentShaderMembers[] = {
    {"outSize", "vec2", 0, 8},
    {"threshold", "float", 8, 4},
};

constexpr ShaderSampler kPostBrightFragmentShaderSamplers[] = {
    {"srcTex", 1, 1, 0, 1},
};

constexpr ShaderBlockLayout::Member kPostBlurFragmentShaderMembers[] = {
    {"outSize", "vec2", 0, 8},
    {"direction", "vec2", 8, 8},
};

constexpr ShaderSampler kPostBlurFragmentShaderSamplers[] = {
    {"srcTex", 1, 1, 0, 1},
};

constexpr ShaderBlockLayout::Member kPostTonemapFragmentShaderMembers[] = {
    {"useSSAO", "int", 0, 4},
    {"useVolumetric", "int", 4, 4},
    {"outSize", "vec2", 8, 8},
    {"exposure", "float", 16, 4},
};

constexpr ShaderSampler kPostTonemapFragmentShaderSamplers[] = {
    {"hdrTex", 1, 1, 0, 1},
    {"hdrAmbient", 2, 1, 1, 1},
    {"bloomTex", 3, 1, 2, 1},
    {"ssaoTex", 4, 1, 3, 1},
    {"volTex", 5, 1, 4, 1},
};

constexpr ShaderBlockLayout::Member kPostSSAOFragmentShaderMembers[] = {
    {"invProj", "mat4", 0, 64},
    {"proj", "mat4", 64, 64},
    {"outSize", "vec2", 128, 8},
    {"radius", "float", 136, 4},
    {"sampleCount", "int", 140, 4},
    {"bias", "float", 144, 4},
};

constexpr ShaderSampler kPostSSAOFragmentShaderSamplers[] = {
    {"depthTex", 1, 1, 0, 1},
};

constexpr ShaderBlockLayout::Member kPostSSAOStfFragmentShaderMembers[] = {
    {"invProj", "mat4", 0, 64},
    {"invView", "mat4", 64, 64},
    {"prevViewProj", "mat4", 128, 64},
    {"outSize", "vec2", 192, 8},
    {"blend", "float", 200, 4},
    {"depthRejection", "float", 204, 4},
    {"hasHistory", "int", 208, 4},
};

constexpr ShaderSampler kPostSSAOStfFragmentShaderSamplers[] = {
    {"depthTex", 1, 1, 0, 1},
    {"ssaoRawTex", 2, 1, 1, 1},
    {"historyTex", 3, 1, 2, 1},
    {"historyDepthTex", 4, 1, 3, 1},
};

constexpr ShaderBlockLayout::Member kPostMLAAFragmentShaderMembers[] = {
    {"outSize", "vec2", 0, 8},
    {"lumThreshold", "float", 8, 4},
    {"depthThreshold", "float", 12, 4},
};

constexpr ShaderSampler kPostMLAAFragmentShaderSamplers[] = {
    {"srcTex", 1, 1, 0, 1},
    {"depthTex", 2, 1, 0, 1},
};

constexpr ShaderBlockLayout::Member kPostBlitFragmentShaderMembers[] = {
    {nullptr, nullptr, 0, 0},
};

constexpr ShaderSampler kPostBlitFragmentShaderSamplers[] = {
    {"srcTex", 1, 1, 0, 1},
};

constexpr ShaderBlockLayout::Member kPostDOFFragmentShaderMembers[] = {
    {"invProj", "mat4", 0, 64},
    {"outSize", "vec2", 64, 8},
    {"tanHalfFovY", "float", 72, 4},
    {"focusDistance", "float", 76, 4},
    {"aperture", "float", 80, 4},
    {"maxCocPixels", "float", 84, 4},
};

constexpr ShaderSampler kPostDOFFragmentShaderSamplers[] = {
    {"srcTex", 1, 1, 0, 1},
    {"srcAmbient", 2, 1, 1, 1},
    {"depthTex", 3, 1, 0, 1},
};

constexpr ShaderBlockLayout::Member kPostVolumetricFragmentShaderMembers[] = {
    {"invProj", "mat4", 0, 64},
    {"invView", "mat4", 64, 64},
    {"cameraPos", "vec3", 128, 12},
    {"outSize", "vec2", 144, 8},
    {"volCount", "int", 152, 4},
    {"volPos", "vec3", 160, 64},
    {"volDir", "vec3", 224, 64},
    {"volColor", "vec3", 288, 64},
    {"volIntensity", "float", 352, 64},
    {"volRadius", "float", 416, 64},
    {"volInnerCos", "float", 480, 64},
    {"volOuterCos", "float", 544, 64},
    {"volIsCone", "int", 608, 64},
    {"volDensity", "float", 672, 64},
    {"volShadowSlot", "int", 736, 64},
    {"pointLightViews", "mat4", 832, 128},
    {"pointLightRange", "vec2", 960, 32},
    {"pointShadowBias", "float", 992, 32},
};

constexpr ShaderSampler kPostVolumetricFragmentShaderSamplers[] = {
    {"depthTex", 1, 1, 0, 1},
    {"pointShadowFront", 2, 2, 4, 2},
    {"pointShadowBack", 3, 2, 5, 2},
};

constexpr ShaderBlockLayout::Member kVolumeInjectFragmentShaderMembers[] = {
    {"volumeOrigin", "vec3", 0, 12},
    {"volumeExtent", "vec3", 16, 12},
    {"outSize", "vec2", 32, 8},
    {"layerCoord", "float", 40, 4},
    {"lightCount", "int", 44, 4},
    {"lightType", "int", 48, 64},
    {"lightPos", "vec3", 112, 64},
    {"lightDir", "vec3", 176, 64},
    {"lightColor", "vec3", 240, 64},
    {"lightIntensity", "float", 304, 64},
    {"lightRange", "float", 368, 64},
    {"lightInner", "float", 432, 64},
    {"lightOuter", "float", 496, 64},
};

constexpr ShaderSampler kVolumeInjectFragmentShaderSamplers[] = {
    {nullptr, 0, 0, 0, 1},
};

constexpr ShaderBlockLayout::Member kVolumePropagateFragmentShaderMembers[] = {
    {"texelSize", "vec3", 0, 12},
    {"outSize", "vec2", 16, 8},
    {"layerCoord", "float", 24, 4},
};

constexpr ShaderSampler kVolumePropagateFragmentShaderSamplers[] = {
    {"volumeIn", 1, 1, 0, 1},
};

// The layout table itself, indexed by ShaderProgramId order: the
// enum's declaration order in grace/render/shader.hpp, which is the
// same order the backend's createShaderProgram resolves.
constexpr ShaderLayoutEntry kShaderLayouts[] = {
    {
        "kTriangleVertexShader",
        kTriangleVertexShaderMembers,
        1,
        kTriangleVertexShaderSamplers,
        0,
    },
    {
        "kTriangleFragmentShader",
        kTriangleFragmentShaderMembers,
        1,
        kTriangleFragmentShaderSamplers,
        0,
    },
    {
        "kLightingVertexShader",
        kLightingVertexShaderMembers,
        3,
        kLightingVertexShaderSamplers,
        0,
    },
    {
        "kLightingFragmentShader",
        kLightingFragmentShaderMembers,
        38,
        kLightingFragmentShaderSamplers,
        4,
    },
    {
        "kShadowDepthVertexShader",
        kShadowDepthVertexShaderMembers,
        1,
        kShadowDepthVertexShaderSamplers,
        0,
    },
    {
        "kShadowDepthFragmentShader",
        kShadowDepthFragmentShaderMembers,
        0,
        kShadowDepthFragmentShaderSamplers,
        0,
    },
    {
        "kParaboloidDepthVertexShader",
        kParaboloidDepthVertexShaderMembers,
        5,
        kParaboloidDepthVertexShaderSamplers,
        0,
    },
    {
        "kParaboloidDepthFragmentShader",
        kParaboloidDepthFragmentShaderMembers,
        2,
        kParaboloidDepthFragmentShaderSamplers,
        0,
    },
    {
        "kGBufferVertexShader",
        kGBufferVertexShaderMembers,
        2,
        kGBufferVertexShaderSamplers,
        0,
    },
    {
        "kGBufferFragmentShader",
        kGBufferFragmentShaderMembers,
        3,
        kGBufferFragmentShaderSamplers,
        0,
    },
    {
        "kDeferredLightVertexShader",
        kDeferredLightVertexShaderMembers,
        0,
        kDeferredLightVertexShaderSamplers,
        0,
    },
    {
        "kDeferredLightFragmentShader",
        kDeferredLightFragmentShaderMembers,
        36,
        kDeferredLightFragmentShaderSamplers,
        7,
    },
    {
        "kFlatVertexShader",
        kFlatVertexShaderMembers,
        0,
        kFlatVertexShaderSamplers,
        0,
    },
    {
        "kFlatFragmentShader",
        kFlatFragmentShaderMembers,
        1,
        kFlatFragmentShaderSamplers,
        0,
    },
    {
        "kBrokenVertexShader",
        kBrokenVertexShaderMembers,
        0,
        kBrokenVertexShaderSamplers,
        0,
    },
    {
        "kBrokenFragmentShader",
        kBrokenFragmentShaderMembers,
        0,
        kBrokenFragmentShaderSamplers,
        0,
    },
    {
        "kPostBrightFragmentShader",
        kPostBrightFragmentShaderMembers,
        2,
        kPostBrightFragmentShaderSamplers,
        1,
    },
    {
        "kPostBlurFragmentShader",
        kPostBlurFragmentShaderMembers,
        2,
        kPostBlurFragmentShaderSamplers,
        1,
    },
    {
        "kPostTonemapFragmentShader",
        kPostTonemapFragmentShaderMembers,
        4,
        kPostTonemapFragmentShaderSamplers,
        5,
    },
    {
        "kPostSSAOFragmentShader",
        kPostSSAOFragmentShaderMembers,
        6,
        kPostSSAOFragmentShaderSamplers,
        1,
    },
    {
        "kPostSSAOStfFragmentShader",
        kPostSSAOStfFragmentShaderMembers,
        7,
        kPostSSAOStfFragmentShaderSamplers,
        4,
    },
    {
        "kPostMLAAFragmentShader",
        kPostMLAAFragmentShaderMembers,
        3,
        kPostMLAAFragmentShaderSamplers,
        2,
    },
    {
        "kPostBlitFragmentShader",
        kPostBlitFragmentShaderMembers,
        0,
        kPostBlitFragmentShaderSamplers,
        1,
    },
    {
        "kPostDOFFragmentShader",
        kPostDOFFragmentShaderMembers,
        6,
        kPostDOFFragmentShaderSamplers,
        3,
    },
    {
        "kPostVolumetricFragmentShader",
        kPostVolumetricFragmentShaderMembers,
        18,
        kPostVolumetricFragmentShaderSamplers,
        3,
    },
    {
        "kVolumeInjectFragmentShader",
        kVolumeInjectFragmentShaderMembers,
        13,
        kVolumeInjectFragmentShaderSamplers,
        0,
    },
    {
        "kVolumePropagateFragmentShader",
        kVolumePropagateFragmentShaderMembers,
        3,
        kVolumePropagateFragmentShaderSamplers,
        1,
    },
};

}  // namespace grace::render::vulkan
