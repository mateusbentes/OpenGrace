#pragma once

// Internal GLSL sources for the forward lighting pass of the OpenGL backend.
// This header lives in src/ and is not part of any public API-neutral header.

namespace grace::render::opengl {

// Vertex stage: transforms by model/mvp and passes world position and
// normal to the fragment stage. Normals use mat3(model), which is exact for
// rotation/uniform scale (the documented limitation for non-uniform scale).
constexpr const char* kLightingVertexShader = R"(#version 330 core
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

// Fragment stage: Lambert diffuse + Blinn-Phong specular. The sun
// (directional light) is handled separately from the dynamic lights, as the
// architecture document describes. Attenuation matches the documented models
// in grace/render/light.hpp exactly (windowed inverse square, smoothstep
// cone falloff).
constexpr const char* kLightingFragmentShader = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
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
// Single-cascade sun shadow map (milestone 4b). The shadow factor applies to
// the sun's contribution only; dynamic lights stay unshadowed (documented).
uniform sampler2D shadowMap;
uniform int hasShadowMap;
uniform mat4 lightSpaceMatrix;
uniform float shadowBias;
out vec4 fragment;

float rangeAtten(float intensity, float range, float d)
{
    if (d > range) return 0.0;
    float w = 1.0 - d / range;
    return intensity * w * w / (d * d + 1.0);
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPos - vWorldPos);
    vec3 result = emissive;
    if (hasSun == 1)
    {
        vec3 L = normalize(-sunDirection);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), shininess);
        // Shadow lookup: world position -> light space -> [0,1] uv/depth,
        // compared against the stored depth with a constant bias.
        float shadow = 1.0;
        if (hasShadowMap == 1)
        {
            vec4 lp = lightSpaceMatrix * vec4(vWorldPos, 1.0);
            vec3 proj = lp.xyz / lp.w;
            vec2 uv = proj.xy * 0.5 + 0.5;
            float depth = proj.z * 0.5 + 0.5;
            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && depth <= 1.0)
            {
                float stored = texture(shadowMap, uv).r;
                shadow = (depth <= stored + shadowBias) ? 1.0 : 0.0;
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

// Depth-only shadow pass (milestone 4b): vertex stage transforms by the
// light-space MVP; the fragment stage writes no colour (depth is written by
// the rasterizer into the depth-only target).
constexpr const char* kShadowVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 lightMVP;
void main()
{
    gl_Position = lightMVP * vec4(position, 1.0);
}
)";

constexpr const char* kShadowFragmentShader = R"(#version 330 core
void main()
{
}
)";

}  // namespace grace::render::opengl
