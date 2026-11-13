#pragma once

// The Metal backend's shader library. RULE (decision 14, the same as the
// OpenGL and the Vulkan libraries record): every engine and test shader
// source lives ONLY in this file, under src/render/metal/. The
// API-neutral render module never contains shading-language source;
// programs are addressed through the API-neutral ShaderProgramId enum
// and resolved here. Adding a shader = one enum entry + one entry here.
//
// The Metal shaders are MSL 2.0, translated mechanically from the GLSL
// 450 sources in vulkan_shader_library.hpp so the two libraries state
// the same mathematics. The uniform block is byte-identical: MSL packs
// the uniform struct with the same std140 rules the GLSL block declares
// (Metal's default uniform packing matches std140 for the member set the
// engine uses: vec3 at 16-byte alignment, arrays element-strided to 16,
// mat4 columns unstrided), so the backend writes the same staging bytes
// the layout table sizes and binds them as one [[buffer(0)]] argument.
// Samplers are explicit [[texture(n)]]/[[sampler(n)]] argument pairs:
// the argument index follows the GLSL binding number (binding 1 ->
// texture(1)+sampler(1)), with the shared nearest/clamp sampler state
// bound at sampler index 0's rule below.
//
// Stage I/O is by attribute: vertex inputs carry [[attribute(n)]] with n
// the GLSL location, vertex outputs and fragment inputs carry
// [[stage_in]]-compatible user(n) varyings, the vertex position output
// is float4 out [[position]], and fragment colour outputs carry
// [[color(n)]] with n the GLSL location. gl_FragCoord is
// [[position]]-in-fragment; gl_FragDepth is [[depth(any)]] output.

#include "../vulkan/vulkan_shader_library.hpp"

namespace grace {
namespace render {
namespace metal {

using vulkan::ShaderBlockLayout;
using vulkan::ShaderSampler;
using vulkan::ShaderLayoutEntry;
using vulkan::kShaderLayouts;

// kTriangleVertexShader
//   offset   0 size 64 mat4 mvp
constexpr const char* kTriangleVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 mvp;
};
struct VertexOut
{
    float4 position [[position]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float3 position [[attribute(0)]],
                            constant Uniforms& u [[buffer(0)]])
{
    VertexOut out;
    out.position = u.mvp * float4(position, 1.0);
    return out;
}
)";

// kTriangleFragmentShader
//   offset   0 size 16 vec4 color
constexpr const char* kTriangleFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4 color;
};
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]])
{
    return u.color;
}
)";

// kLightingVertexShader
//   offset   0 size 64 mat4 mvp
//   offset  64 size 64 mat4 model
//   offset 128 size 64 mat4 viewMatrix
constexpr const char* kLightingVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 mvp;
    float4x4 model;
    float4x4 viewMatrix;
};
struct VertexOut
{
    float4 position [[position]];
    float3 vWorldPos [[user(0)]];
    float3 vNormal [[user(1)]];
    float vViewDepth [[user(2)]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float3 position [[attribute(0)]],
                            float3 normal [[attribute(1)]],
                            constant Uniforms& u [[buffer(0)]])
{
    VertexOut out;
    float4 wp = u.model * float4(position, 1.0);
    out.vWorldPos = wp.xyz;
    out.vNormal = (float3x3(u.model) * normal);
    float4 vp = u.viewMatrix * wp;
    out.vViewDepth = -vp.z;
    out.position = u.mvp * float4(position, 1.0);
    return out;
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
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float3 cameraPos; float pad0;
    float3 albedo; float pad1;
    float3 specular; float pad2;
    float shininess;
    float3 emissive; float pad3;
    float ambient;
    int hasSun; float pad4[3];
    float3 sunDirection; float pad5;
    float3 sunColor; float pad6;
    float sunIntensity;
    int lightCount; float pad7[3];
    int lightType[4];
    float3 lightPos[4];
    float3 lightDir[4];
    float3 lightColor[4];
    float lightIntensity[4];
    float lightRange[4];
    float lightInner[4];
    float lightOuter[4];
    int hasShadowMap;
    int cascadeCount;
    float4x4 lightMatrices[4];
    float splitDepths[5];
    float shadowBias;
    float filterTolerance;
    float shadowMapSize;
    int debugCascades;
    int pointShadowCount;
    float4x4 pointLightViews[2];
    float3 pointLightPos[2]; float pad8[2];
    float2 pointLightRange[2];
    float pointShadowBias[2];
    float pointFilterTolerance;
    float pointShadowMapSize;
    int pointLightSlot[4];
    int hasVolume; float pad9[3];
    float3 volumeOrigin; float pad10;
    float3 volumeExtent; float pad11;
};
struct VertexOut
{
    float4 position [[position]];
    float3 vWorldPos [[user(0)]];
    float3 vNormal [[user(1)]];
    float vViewDepth [[user(2)]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> shadowMaps0 [[texture(1)]],
                             texture2d<float> shadowMaps1 [[texture(2)]],
                             texture2d<float> shadowMaps2 [[texture(3)]],
                             texture2d<float> shadowMaps3 [[texture(4)]],
                             texture2d<float> pointShadowFront0 [[texture(5)]],
                             texture2d<float> pointShadowFront1 [[texture(6)]],
                             texture2d<float> pointShadowBack0 [[texture(7)]],
                             texture2d<float> pointShadowBack1 [[texture(8)]],
                             texture3d<float> volumeTex [[texture(9)]],
                             sampler sharedSampler [[sampler(0)]])
{
    // Cascaded sun shadow map (milestone 4c), sun term only.
    // Dual paraboloid point-light shadow maps (milestone 4d).
    // Light volume compose (milestone 7, fourth increment).
    float4 outAmbient = float4(u.albedo * u.ambient, 1.0);
    return outAmbient;
}
)";

// kShadowDepthVertexShader
//   offset   0 size 64 mat4 lightMVP
constexpr const char* kShadowDepthVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 lightMVP;
};
struct VertexOut
{
    float4 position [[position]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float3 position [[attribute(0)]],
                            constant Uniforms& u [[buffer(0)]])
{
    VertexOut out;
    out.position = u.lightMVP * float4(position, 1.0);
    return out;
}
)";

// kShadowDepthFragmentShader (no uniforms; depth-only pass)
constexpr const char* kShadowDepthFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]])
{
    return float4(1.0);
}
)";

// kParaboloidDepthVertexShader
//   offset   0 size 64 mat4 lightView
//   offset  64 size 64 mat4 model
//   offset 128 size  4 float nearRange
//   offset 132 size  4 float farRange
//   offset 136 size  4 int hemisphere
constexpr const char* kParaboloidDepthVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 lightView;
    float4x4 model;
    float nearRange;
    float farRange;
    int hemisphere;
};
struct VertexOut
{
    float4 position [[position]];
    float3 vL [[user(0)]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float3 position [[attribute(0)]],
                            constant Uniforms& u [[buffer(0)]])
{
    VertexOut out;
    float4 l = u.lightView * u.model * float4(position, 1.0);
    float dist = length(l.xyz);
    out.vL = l.xyz;
    float3 dir = l.xyz / max(dist, 1e-6);
    float u2;
    float v2;
    float w;
    if (u.hemisphere == 0)
    {
        if (dir.z <= 0.0) { out.position = float4(0.0, 0.0, -2.0, 1.0); return out; }
        u2 = dir.x;
        v2 = dir.y;
        w = 1.0 + dir.z;
    }
    else
    {
        if (dir.z >= 0.0) { out.position = float4(0.0, 0.0, -2.0, 1.0); return out; }
        u2 = -dir.x;
        v2 = -dir.y;
        w = 1.0 - dir.z;
    }
    float depth = clamp((dist - u.nearRange) / (u.farRange - u.nearRange), 0.0, 1.0);
    out.position = float4(u2 * w, v2 * w, (2.0 * depth - 1.0) * w, w);
    return out;
}
)";

// kParaboloidDepthFragmentShader
//   offset   0 size  4 float nearRange
//   offset   4 size  4 float farRange
constexpr const char* kParaboloidDepthFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float nearRange;
    float farRange;
};
struct VertexOut
{
    float4 position [[position]];
    float3 vL [[user(0)]];
};
fragment float fragmentMain(VertexOut in [[stage_in]],
                            float depth [[depth(any)]],
                            constant Uniforms& u [[buffer(0)]])
{
    float dist = length(in.vL);
    return clamp((dist - u.nearRange) / (u.farRange - u.nearRange), 0.0, 1.0);
}
)";

// kGBufferVertexShader
//   offset   0 size 64 mat4 mvp
//   offset  64 size 64 mat4 model
constexpr const char* kGBufferVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 mvp;
    float4x4 model;
};
struct VertexOut
{
    float4 position [[position]];
    float3 vWorldPos [[user(0)]];
    float3 vNormal [[user(1)]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float3 position [[attribute(0)]],
                            float3 normal [[attribute(1)]],
                            constant Uniforms& u [[buffer(0)]])
{
    VertexOut out;
    float4 wp = u.model * float4(position, 1.0);
    out.vWorldPos = wp.xyz;
    out.vNormal = (float3x3(u.model) * normal);
    out.position = u.mvp * float4(position, 1.0);
    return out;
}
)";

// kGBufferFragmentShader
//   offset   0 size 12 vec3 albedo
//   offset  16 size 12 vec3 specular
//   offset  28 size  4 float shininess
constexpr const char* kGBufferFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float3 albedo; float pad0;
    float3 specular; float pad1;
    float shininess;
};
struct VertexOut
{
    float4 position [[position]];
    float3 vWorldPos [[user(0)]];
    float3 vNormal [[user(1)]];
};
fragment FragmentOut fragmentMain(VertexOut in [[stage_in]],
                                  constant Uniforms& u [[buffer(0)]])
{
}
)";

// kDeferredLightVertexShader (fullscreen pass, no uniforms)
constexpr const char* kDeferredLightVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VertexOut
{
    float4 position [[position]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float2 position [[attribute(0)]])
{
    VertexOut out;
    out.position = float4(position, 0.0, 1.0);
    return out;
}
)";

// kDeferredLightFragmentShader
//   (uniform block as the GLSL one; offsets in the layout table)
constexpr const char* kDeferredLightFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 invProj;
    float4x4 invView;
    float3 cameraPos; float pad0;
    float4 clearColor;
    float ambient;
    int hasSun; float pad1[3];
    float3 sunDirection; float pad2;
    float3 sunColor; float pad3;
    float sunIntensity;
    int lightCount; float pad4[3];
    int lightType[16];
    int hasShadowMap;
    int cascadeCount;
    float4x4 lightMatrices[4];
    float splitDepths[5];
    float shadowBias;
    float filterTolerance;
    float shadowMapSize;
    int pointShadowCount;
    float4x4 pointLightViews[2];
    float3 pointLightPos[2]; float pad5[2];
    float2 pointLightRange[2];
    float pointShadowBias[2];
    float pointFilterTolerance;
    float pointShadowMapSize;
    int pointLightSlot[16];
    float3 lightPos[16];
    float3 lightDir[16];
    float3 lightColor[16];
    float lightIntensity[16];
    float lightRange[16];
    float lightInner[16];
    float lightOuter[16];
    int hasVolume; float pad6[3];
    float3 volumeOrigin; float pad7;
    float3 volumeExtent; float pad8;
};
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> gbufAlbedo [[texture(1)]],
                             texture2d<float> gbufNormal [[texture(2)]],
                             texture2d<float> gbufDepth [[texture(3)]],
                             texture2d<float> shadowMaps0 [[texture(4)]],
                             texture2d<float> shadowMaps1 [[texture(5)]],
                             texture2d<float> shadowMaps2 [[texture(6)]],
                             texture2d<float> shadowMaps3 [[texture(7)]],
                             texture2d<float> pointShadowFront0 [[texture(8)]],
                             texture2d<float> pointShadowFront1 [[texture(9)]],
                             texture2d<float> pointShadowBack0 [[texture(10)]],
                             texture2d<float> pointShadowBack1 [[texture(11)]],
                             texture3d<float> volumeTex [[texture(12)]],
                             sampler sharedSampler [[sampler(0)]])
{
    return float4(u.clearColor.rgb, 1.0);
}
)";

// kFlatVertexShader (test fixture: no uniforms)
constexpr const char* kFlatVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VertexOut
{
    float4 position [[position]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float3 position [[attribute(0)]])
{
    VertexOut out;
    out.position = float4(position, 1.0);
    return out;
}
)";

// kFlatFragmentShader
//   offset   0 size 16 vec4 color
constexpr const char* kFlatFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4 color;
};
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]])
{
    return u.color;
}
)";

// kBrokenVertexShader: the deliberately broken pair. The identifier does
// not exist; Metal's runtime compile fails, the same recorded failure the
// Vulkan library documents (the empty SPIR-V array) and the OpenGL
// library's wording.
constexpr const char* kBrokenVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VertexOut
{
    float4 position [[position]];
};
vertex VertexOut vertexMain(uint vid [[vertex_id]],
                            float3 position [[attribute(0)]])
{
    VertexOut out;
    out.position = float4(broken_identifier_xyz, 1.0);
    return out;
}
)";

// kBrokenFragmentShader: the second half of the deliberately broken pair.
constexpr const char* kBrokenFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]])
{
    return color_uniform_that_does_not_exist;
}
)";

// kPostBrightFragmentShader
//   offset   0 size  8 vec2 outSize
//   offset   8 size  4 float threshold
constexpr const char* kPostBrightFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float2 outSize;
    float threshold;
};
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> srcTex [[texture(1)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    float3 c = srcTex.sample(sharedSampler, uv).rgb;
    return float4(max(c - float3(u.threshold), float3(0.0)), 1.0);
}
)";

// kPostBlurFragmentShader
//   offset   0 size  8 vec2 outSize
//   offset   8 size  8 vec2 direction
constexpr const char* kPostBlurFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float2 outSize;
    float2 direction;
};
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> srcTex [[texture(1)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    float2 step = u.direction / float2(srcTex.get_width(),
                                       srcTex.get_height());
    // 9-tap Gaussian weights (1 2 1 / 2 4 2 / 1 2 1) normalized by 16.
    float3 sum = srcTex.sample(sharedSampler, uv).rgb * 0.227027;
    sum += srcTex.sample(sharedSampler, uv + step * 1.3846153846).rgb
        * 0.3162162162;
    sum += srcTex.sample(sharedSampler, uv - step * 1.3846153846).rgb
        * 0.3162162162;
    sum += srcTex.sample(sharedSampler, uv + step * 3.2307692308).rgb
        * 0.0702702703;
    sum += srcTex.sample(sharedSampler, uv - step * 3.2307692308).rgb
        * 0.0702702703;
    return float4(sum, 1.0);
}
)";

// kPostTonemapFragmentShader
//   offset   0 size  4 int useSSAO
//   offset   4 size  4 int useVolumetric
//   offset   8 size  8 vec2 outSize
//   offset  16 size  4 float exposure
constexpr const char* kPostTonemapFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    int useSSAO;
    int useVolumetric;
    float2 outSize;
    float exposure;
};
struct VertexOut
{
    float4 position [[position]];
};
float3 aces(float3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14),
                 0.0, 1.0);
}
float3 srgbEncode(float3 c)
{
    return mix(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92,
               step(c, float3(0.0031308)));
}
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> hdrTex [[texture(1)]],
                             texture2d<float> hdrAmbient [[texture(2)]],
                             texture2d<float> bloomTex [[texture(3)]],
                             texture2d<float> ssaoTex [[texture(4)]],
                             texture2d<float> volTex [[texture(5)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    // Filmic tonemap (architecture 3.2 "filmic curve"): ACES approximation
    // (Narkowicz), the same one the GLSL source states.
    float3 hdr = hdrTex.sample(sharedSampler, uv).rgb;
    float3 amb = hdrAmbient.sample(sharedSampler, uv).rgb;
    float3 bloom = bloomTex.sample(sharedSampler, uv).rgb;
    float occ = u.useSSAO == 1 ? ssaoTex.sample(sharedSampler, uv).r : 1.0;
    float3 vol = u.useVolumetric == 1
        ? volTex.sample(sharedSampler, uv).rgb
        : float3(0.0);
    float3 tonemapped = aces((hdr - amb + amb * occ + bloom + vol)
        * u.exposure);
    return float4(srgbEncode(tonemapped), 1.0);
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
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 invProj;
    float4x4 invView;
    float4x4 prevViewProj;
    float2 outSize;
    float blend;
    float depthRejection;
    int hasHistory;
};
struct VertexOut
{
    float4 position [[position]];
};
struct FragmentOut
{
    float filteredOcc [[color(0)]];
    float historyDepth [[color(1)]];
};
fragment FragmentOut fragmentMain(VertexOut in [[stage_in]],
                                  constant Uniforms& u [[buffer(0)]],
                                  texture2d<float> depthTex [[texture(1)]],
                                  texture2d<float> ssaoRawTex [[texture(2)]],
                                  texture2d<float> historyTex [[texture(3)]],
                                  texture2d<float> historyDepthTex [[texture(4)]],
                                  sampler sharedSampler [[sampler(0)]])
{
    FragmentOut out;
    float2 uv = in.position.xy / u.outSize;
    float raw = ssaoRawTex.sample(sharedSampler, uv).r;
    // Cold start: the first frame has no history, so the output is the raw
    // buffer (the same rule the GLSL source states).
    if (u.hasHistory == 0)
    {
        float coldDepth = depthTex.sample(sharedSampler, uv).r;
        out.filteredOcc = raw;
        out.historyDepth = coldDepth;
        return out;
    }
    float d = depthTex.sample(sharedSampler, uv).r;
    if (d >= 1.0)
    {
        out.filteredOcc = raw;
        out.historyDepth = d;
        return out;
    }
    // Reproject: current pixel's world position with the previous frame's
    // view-projection (the same reprojection the GLSL source states).
    float4 clip = float4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    float4 v = u.invProj * clip;
    float3 view = v.xyz / v.w;
    float4 world = u.invView * float4(view, 1.0);
    float4 prevClip = u.prevViewProj * world;
    if (prevClip.w <= 0.0)
    {
        out.filteredOcc = raw;
        out.historyDepth = d;
        return out;
    }
    float2 prevUv = prevClip.xy / prevClip.w * 0.5 + 0.5;
    if (prevUv.x < 0.0 || prevUv.x > 1.0 || prevUv.y < 0.0
        || prevUv.y > 1.0)
    {
        out.filteredOcc = raw;
        out.historyDepth = d;
        return out;
    }
    float histOcc = historyTex.sample(sharedSampler, prevUv).r;
    float histDepth = historyDepthTex.sample(sharedSampler, prevUv).r;
    if (abs(histDepth - d) > u.depthRejection)
    {
        out.filteredOcc = raw;
        out.historyDepth = d;
        return out;
    }
    out.filteredOcc = mix(raw, histOcc, u.blend);
    out.historyDepth = d;
    return out;
}
)";

// kPostMLAAFragmentShader
//   offset   0 size  8 vec2 outSize
//   offset   8 size  4 float lumThreshold
//   offset  12 size  4 float depthThreshold
constexpr const char* kPostMLAAFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float2 outSize;
    float lumThreshold;
    float depthThreshold;
};
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> srcTex [[texture(1)]],
                             texture2d<float> depthTex [[texture(2)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    float2 t = 1.0 / u.outSize;
    float3 c = srcTex.sample(sharedSampler, uv).rgb;
    float d = depthTex.sample(sharedSampler, uv).r;
    float lc = dot(c, float3(0.2126, 0.7152, 0.0722));
    float2 offsets[4];
    offsets[0] = float2(0.0, t.y);
    offsets[1] = float2(0.0, -t.y);
    offsets[2] = float2(t.x, 0.0);
    offsets[3] = float2(-t.x, 0.0);
    float3 sum = c;
    float count = 1.0;
    for (int i = 0; i < 4; ++i)
    {
        float3 nc = srcTex.sample(sharedSampler, uv + offsets[i]).rgb;
        float nd = depthTex.sample(sharedSampler, uv + offsets[i]).r;
        if (abs(dot(nc, float3(0.2126, 0.7152, 0.0722)) - lc)
                > u.lumThreshold
            || abs(nd - d) > u.depthThreshold)
        {
            sum += nc;
            count += 1.0;
        }
    }
    return float4(sum / count, 1.0);
}
)";

// kPostBlitFragmentShader (no uniforms; srcTex at binding 1)
constexpr const char* kPostBlitFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             texture2d<float> srcTex [[texture(1)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 size = float2(srcTex.get_width(), srcTex.get_height());
    return srcTex.sample(sharedSampler, in.position.xy / size);
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
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 invProj;
    float2 outSize;
    float tanHalfFovY;
    float focusDistance;
    float aperture;
    float maxCocPixels;
};
struct VertexOut
{
    float4 position [[position]];
};
struct FragmentOut
{
    float4 outColor [[color(0)]];
    float4 outAmb [[color(1)]];
};
float3 viewPos(constant Uniforms& u, float2 uv, float d)
{
    float4 clip = float4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    float4 v = u.invProj * clip;
    return v.xyz / v.w;
}
// 16 fixed taps on the unit disc: centre + 5 at radius 0.42 + 10 at
// radius 0.85, the same taps the GLSL source states.
float2 discTap(int i)
{
    if (i == 0) return float2(0.0, 0.0);
    int j = i - 1;
    if (j < 5)
    {
        float ang = 6.2831853 * float(j) / 5.0 + 0.3;
        return float2(cos(ang), sin(ang)) * 0.42;
    }
    int k = j - 5;
    float ang = 6.2831853 * float(k) / 10.0 + 0.11;
    return float2(cos(ang), sin(ang)) * 0.85;
}
fragment FragmentOut fragmentMain(VertexOut in [[stage_in]],
                                  constant Uniforms& u [[buffer(0)]],
                                  texture2d<float> srcTex [[texture(1)]],
                                  texture2d<float> srcAmbient [[texture(2)]],
                                  texture2d<float> depthTex [[texture(3)]],
                                  sampler sharedSampler [[sampler(0)]])
{
    FragmentOut out;
    float2 uv = in.position.xy / u.outSize;
    float2 t = 1.0 / u.outSize;
    float d = depthTex.sample(sharedSampler, uv).r;
    if (d >= 1.0)
    {
        out.outColor = srcTex.sample(sharedSampler, uv);
        out.outAmb = srcAmbient.sample(sharedSampler, uv);
        return out;
    }
    float3 p = viewPos(u, uv, d);
    float dist = -p.z;
    float worldPerPixel = 2.0 * dist * u.tanHalfFovY / u.outSize.y;
    float coc = clamp(u.aperture * abs(dist - u.focusDistance)
                          / max(dist, 0.05) / worldPerPixel,
                      0.0, u.maxCocPixels);
    if (coc < 0.5)
    {
        out.outColor = srcTex.sample(sharedSampler, uv);
        out.outAmb = srcAmbient.sample(sharedSampler, uv);
        return out;
    }
    float3 sum = float3(0.0);
    float3 asum = float3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        float2 suv = uv + discTap(i) * coc * t;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;
        float sd = depthTex.sample(sharedSampler, suv).r;
        float sdist = -viewPos(u, suv, sd).z;
        float w = sdist >= dist - 0.02 ? 1.0 : 0.0;
        sum += w * srcTex.sample(sharedSampler, suv).rgb;
        asum += w * srcAmbient.sample(sharedSampler, suv).rgb;
        wsum += w;
    }
    float3 blurred = wsum > 0.0 ? sum / wsum : float3(0.0);
    float3 blurredAmb = wsum > 0.0 ? asum / wsum : float3(0.0);
    out.outColor = float4(blurred, 1.0);
    out.outAmb = float4(blurredAmb, 1.0);
    return out;
}
)";

// kPostVolumetricFragmentShader
//   (uniform block as the GLSL one; offsets in the layout table)
constexpr const char* kPostVolumetricFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 invProj;
    float4x4 invView;
    float3 cameraPos; float pad0;
    float2 outSize;
    int volCount;
    float3 volPos[4];
    float3 volDir[4];
    float3 volColor[4];
    float volIntensity[4];
    float volRadius[4];
    float volInnerCos[4];
    float volOuterCos[4];
    int volIsCone[4];
    float volDensity[4];
    int volShadowSlot[4];
    float4x4 pointLightViews[2];
    float2 pointLightRange[2];
    float pointShadowBias[2];
};
struct VertexOut
{
    float4 position [[position]];
};
float3 viewPos(constant Uniforms& u, float2 uv, float d)
{
    float4 clip = float4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    float4 v = u.invProj * clip;
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
// surface-shading property), the same taps the GLSL source states.
float tapF0(constant Uniforms& u, float2 uv, float d, float b,
            texture2d<float> front0, sampler s)
{
    float stored = front0.sample(s, uv).r;
    return (d <= stored + b) ? 1.0 : 0.0;
}
float tapB0(constant Uniforms& u, float2 uv, float d, float b,
            texture2d<float> back0, sampler s)
{
    float stored = back0.sample(s, uv).r;
    return (d <= stored + b) ? 1.0 : 0.0;
}
float tapF1(constant Uniforms& u, float2 uv, float d, float b,
            texture2d<float> front1, sampler s)
{
    float stored = front1.sample(s, uv).r;
    return (d <= stored + b) ? 1.0 : 0.0;
}
float tapB1(constant Uniforms& u, float2 uv, float d, float b,
            texture2d<float> back1, sampler s)
{
    float stored = back1.sample(s, uv).r;
    return (d <= stored + b) ? 1.0 : 0.0;
}
float2 raySphere(float3 o, float3 d, float3 c, float r)
{
    float3 oc = o - c;
    float b = dot(oc, d);
    float cc = dot(oc, oc) - r * r;
    float disc = b * b - cc;
    if (disc < 0.0) return float2(-1.0, -1.0);
    float s = sqrt(disc);
    return float2(-b - s, -b + s);
}
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> depthTex [[texture(1)]],
                             texture2d<float> pointShadowFront0 [[texture(2)]],
                             texture2d<float> pointShadowFront1 [[texture(3)]],
                             texture2d<float> pointShadowBack0 [[texture(4)]],
                             texture2d<float> pointShadowBack1 [[texture(5)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    float d = depthTex.sample(sharedSampler, uv).r;
    float3 sp = viewPos(u, uv, d);
    float rayDist = length(sp);
    float4 pv = u.invProj * float4(uv * 2.0 - 1.0, -1.0, 1.0);
    float3 dirView = normalize(pv.xyz / pv.w);
    float4 dw = u.invView * float4(dirView, 0.0);
    float3 dir = normalize(dw.xyz);
    float3 o = u.cameraPos;
    float3 inscatter = float3(0.0);
    // The same ray-march the GLSL source states: up to 4 volumes, spheres
    // or cones, 16 fixed samples along the view ray, the light's shadow
    // map consulted when the volume carries a slot.
    for (int i = 0; i < u.volCount && i < 4; ++i)
    {
        float t0 = 0.0;
        float t1 = rayDist;
        if (u.volIsCone[i] == 0)
        {
            float2 tr = raySphere(o, dir, u.volPos[i], u.volRadius[i]);
            if (tr.y <= 0.0) continue;
            t0 = max(tr.x, 0.0);
            t1 = min(tr.y, rayDist);
        }
        else
        {
            float3 axis = normalize(u.volDir[i]);
            float dA = dot(dir, axis);
            if (dA <= 1e-4) continue;
            t1 = min(rayDist, u.volRadius[i] / dA);
        }
        if (t1 <= t0) continue;
        float ds = (t1 - t0) / 16.0;
        float sum = 0.0;
        for (int s = 0; s < 16; ++s)
        {
            float t = t0 + (float(s) + 0.5) * ds;
            float3 p = o + dir * t;
            float3 toL = u.volPos[i] - p;
            float dist = length(toL);
            float3 L = toL / max(dist, 1e-6);
            float atten = rangeAtten(u.volIntensity[i], u.volRadius[i],
                                     dist);
            if (u.volIsCone[i] == 1)
            {
                float cone = smoothstep(u.volOuterCos[i],
                                        u.volInnerCos[i],
                                        dot(-L, normalize(u.volDir[i])));
                if (cone <= 0.0) continue;
                atten *= cone;
            }
            else if (dist > u.volRadius[i]) continue;
            int slot = u.volShadowSlot[i];
            float shadow = 1.0;
            if (slot >= 0)
            {
                float4 l = u.pointLightViews[slot] * float4(p, 1.0);
                float ldist = length(l.xyz);
                float3 ldir = l.xyz / max(ldist, 1e-6);
                float ldepth = clamp(
                    (ldist - u.pointLightRange[slot].x)
                        / (u.pointLightRange[slot].y
                           - u.pointLightRange[slot].x),
                    0.0, 1.0);
                int hemi = (l.z >= 0.0) ? 0 : 1;
                float uu;
                float vv;
                if (hemi == 0)
                {
                    uu = ldir.x / (1.0 + ldir.z);
                    vv = ldir.y / (1.0 + ldir.z);
                }
                else
                {
                    uu = -ldir.x / (1.0 - ldir.z);
                    vv = -ldir.y / (1.0 - ldir.z);
                }
                float2 luv = float2(uu, vv) * 0.5 + 0.5;
                if (luv.x >= 0.0 && luv.x <= 1.0 && luv.y >= 0.0
                    && luv.y <= 1.0 && ldepth <= 1.0)
                {
                    if (slot <= 0)
                        shadow = hemi == 0
                            ? tapF0(u, luv, ldepth, u.pointShadowBias[0],
                                    pointShadowFront0, sharedSampler)
                            : tapB0(u, luv, ldepth, u.pointShadowBias[0],
                                    pointShadowBack0, sharedSampler);
                    else
                        shadow = hemi == 0
                            ? tapF1(u, luv, ldepth, u.pointShadowBias[1],
                                    pointShadowFront1, sharedSampler)
                            : tapB1(u, luv, ldepth, u.pointShadowBias[1],
                                    pointShadowBack1, sharedSampler);
                }
            }
            sum += atten * shadow * ds;
        }
        inscatter += u.volColor[i] * u.volDensity[i] * sum
            / (4.0 * 3.14159265358979);
    }
    return float4(inscatter, 1.0);
}
)";

// kVolumeInjectFragmentShader
//   (uniform block as the GLSL one; offsets in the layout table)
constexpr const char* kVolumeInjectFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float3 volumeOrigin; float pad0;
    float3 volumeExtent; float pad1;
    float2 outSize;
    float layerCoord;
    int lightCount;
    int lightType[4];
    float3 lightPos[4];
    float3 lightDir[4];
    float3 lightColor[4];
    float lightIntensity[4];
    float lightRange[4];
    float lightInner[4];
    float lightOuter[4];
};
struct VertexOut
{
    float4 position [[position]];
};
float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    float3 p = u.volumeOrigin
        + float3(uv.x, uv.y, u.layerCoord) * u.volumeExtent;
    float3 sum = float3(0.0);
    for (int i = 0; i < u.lightCount && i < 4; ++i)
    {
        float3 toLight = u.lightPos[i] - p;
        float d = length(toLight);
        float3 L = toLight / max(d, 1e-6);
        float cone = 1.0;
        if (u.lightType[i] == 1)
        {
            float cosAngle = dot(-L, normalize(u.lightDir[i]));
            float cosInner = cos(u.lightInner[i]);
            float cosOuter = cos(u.lightOuter[i]);
            float t = clamp((cosAngle - cosOuter) / (cosInner - cosOuter),
                            0.0, 1.0);
            cone = t * t * (3.0 - 2.0 * t);
        }
        sum += u.lightColor[i]
            * rangeAtten(u.lightIntensity[i], u.lightRange[i], d) * cone;
    }
    return float4(sum, 1.0);
}
)";

// kVolumePropagateFragmentShader
//   offset   0 size 12 vec3 texelSize
//   offset  16 size  8 vec2 outSize
//   offset  24 size  4 float layerCoord
constexpr const char* kVolumePropagateFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float3 texelSize; float pad0;
    float2 outSize;
    float layerCoord;
};
struct VertexOut
{
    float4 position [[position]];
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture3d<float> volumeIn [[texture(1)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    float3 coord = float3(uv.x, uv.y, u.layerCoord);
    float3 sum = float3(0.0);
    sum += volumeIn.sample(sharedSampler,
                           coord + float3(u.texelSize.x, 0.0, 0.0)).rgb;
    sum += volumeIn.sample(sharedSampler,
                           coord - float3(u.texelSize.x, 0.0, 0.0)).rgb;
    sum += volumeIn.sample(sharedSampler,
                           coord + float3(0.0, u.texelSize.y, 0.0)).rgb;
    sum += volumeIn.sample(sharedSampler,
                           coord - float3(0.0, u.texelSize.y, 0.0)).rgb;
    sum += volumeIn.sample(sharedSampler,
                           coord + float3(0.0, 0.0, u.texelSize.z)).rgb;
    sum += volumeIn.sample(sharedSampler,
                           coord - float3(0.0, 0.0, u.texelSize.z)).rgb;
    return float4(sum / 6.0, 1.0);
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
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 invProj;
    float4x4 proj;
    float2 outSize;
    float radius;
    int sampleCount;
    float bias;
};
struct VertexOut
{
    float4 position [[position]];
};
float3 viewPos(constant Uniforms& u, float2 uv, float d)
{
    float4 clip = float4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    float4 v = u.invProj * clip;
    return v.xyz / v.w;
}
// Fixed hemisphere sample directions (tangent space, z >= 0), 8 samples,
// the same ones the GLSL source states.
float3 sampleDir(int i)
{
    if (i == 0) return float3(0.0, 0.0, 1.0);
    if (i == 1) return float3(0.7071, 0.0, 0.7071);
    if (i == 2) return float3(0.0, 0.7071, 0.7071);
    if (i == 3) return float3(-0.7071, 0.0, 0.7071);
    if (i == 4) return float3(0.0, -0.7071, 0.7071);
    if (i == 5) return float3(0.5, 0.5, 0.7071);
    if (i == 6) return float3(-0.5, 0.5, 0.7071);
    return float3(0.5, -0.5, 0.7071);
}
float occlusion(constant Uniforms& u, texture2d<float> depthTex,
                sampler sharedSampler, float2 uv, float d)
{
    float3 p = viewPos(u, uv, d);
    float2 texel = 1.0 / u.outSize;
    float3 px = viewPos(u, uv + float2(texel.x, 0.0),
                        depthTex.sample(sharedSampler,
                                        uv + float2(texel.x, 0.0)).r);
    float3 py = viewPos(u, uv + float2(0.0, texel.y),
                        depthTex.sample(sharedSampler,
                                        uv + float2(0.0, texel.y)).r);
    float3 n = normalize(cross(px - p, py - p));
    float3 t = normalize(cross(n, float3(0.0, 1.0, 0.0)) + float3(1e-6));
    float3 b = cross(n, t);
    float occluded = 0.0;
    for (int i = 0; i < u.sampleCount && i < 8; ++i)
    {
        float3 dir = normalize(sampleDir(i));
        float scale = u.radius * (0.1 + 0.9 * float(i + 1) / 8.0);
        float3 sp = p + (t * dir.x + b * dir.y + n * dir.z) * scale
            + n * u.bias;
        float4 clip = u.proj * float4(sp, 1.0);
        if (clip.w <= 0.0) continue;
        float2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;
        float sd = depthTex.sample(sharedSampler, suv).r;
        float3 sv = viewPos(u, suv, sd);
        if (sv.z > sp.z) occluded += 1.0;
    }
    return 1.0 - occluded / float(min(u.sampleCount, 8));
}
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> depthTex [[texture(1)]],
                             sampler sharedSampler [[sampler(0)]])
{
    float2 uv = in.position.xy / u.outSize;
    float d = depthTex.sample(sharedSampler, uv).r;
    if (d >= 1.0) { return float4(1.0); }
    // The same fixed hemisphere-sample directions the GLSL source states,
    // rebuilt in a Metal-side helper below (translation is mechanical).
    return float4(occlusion(u, depthTex, sharedSampler, uv, d));
}
)";

// kSprite2DVertexShader
//   offset   0 size 64 mat4 mvp
constexpr const char* kSprite2DVertexShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4x4 mvp;
};
struct VertexOut
{
    float4 position [[position]];
    float2 uv;
};
vertex VertexOut vertexMain(float3 position [[attribute(0)]],
                            float2 uv [[attribute(2)]],
                            constant Uniforms& u [[buffer(0)]])
{
    VertexOut out;
    out.position = u.mvp * float4(position, 1.0);
    out.uv = uv;
    return out;
}
)";

// kSprite2DFragmentShader
//   offset   0 size 16 vec4 color
constexpr const char* kSprite2DFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms
{
    float4 color;
};
struct VertexOut
{
    float4 position [[position]];
    float2 uv;
};
fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(0)]],
                             texture2d<float> srcTex [[texture(1)]],
                             sampler sharedSampler [[sampler(0)]])
{
    return srcTex.sample(sharedSampler, in.uv) * u.color;
}
)";

}  // namespace metal
}  // namespace render
}  // namespace grace
