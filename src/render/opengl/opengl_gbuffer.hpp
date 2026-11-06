#pragma once

// Internal GLSL sources for the deferred path (milestone 5): a G-buffer write
// program (two MRT colour outputs + depth) and a fullscreen lighting pass that
// reconstructs world position from the depth attachment. This header lives in
// src/ and is not part of any public API-neutral header.

namespace grace::render::opengl {

// G-buffer write pass. Attachment 0: rgb = albedo, a = shininess / 1024.
// Attachment 1: rgb = world normal mapped [-1,1] -> [0,1] (8-bit quantization,
// documented precision consequence), a = specular luminance (scalar specular
// in this increment, documented). Depth goes to the depth attachment.
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

// Fullscreen triangle in NDC (positions fed directly as clip coordinates).
constexpr const char* kDeferredLightVertexShader = R"(#version 330 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

// Deferred lighting pass: reads the G-buffer, reconstructs the world position
// from the depth attachment via invProj/invView, and applies the sun plus up
// to kMaxDeferredLights dynamic lights — the per-object four-light limit does
// NOT apply here (documented; the cap is the shader uniform array size, and a
// submission that would exceed it is rejected, not truncated). Shadow maps are
// NOT consumed in this increment (documented). Same Lambert + Blinn-Phong math
// as the forward shader so the paths are comparable.
constexpr const char* kDeferredLightFragmentShader = R"(#version 330 core
uniform sampler2D gbufAlbedo;   // unit 0, attachment 0
uniform sampler2D gbufNormal;   // unit 1, attachment 1
uniform sampler2D gbufDepth;    // unit 2, depth attachment
uniform mat4 invProj;
uniform mat4 invView;
uniform vec3 cameraPos;
uniform int hasSun;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform int lightCount;
uniform int lightType[16];
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

void main()
{
    ivec2 size = textureSize(gbufAlbedo, 0);
    vec2 uv = gl_FragCoord.xy / vec2(size);
    float d = texture(gbufDepth, uv).r;
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

    vec3 result = vec3(0.0);
    if (hasSun == 1)
    {
        vec3 L = normalize(-sunDirection);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        result += albedo * sunColor * sunIntensity * ndl
                + vec3(specLum) * sunColor * sunIntensity * spec * ndl;
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
        result += albedo * lightColor[i] * atten * ndl
                + vec3(specLum) * lightColor[i] * atten * spec * ndl;
    }
    fragment = vec4(clamp(result, 0.0, 1.0), 1.0);
}
)";

}  // namespace grace::render::opengl
