#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "grace/core/math/vec3.hpp"

namespace grace::render {

/// Light parameter conventions (documented units and ranges):
///
/// Colours are linear RGB (not sRGB), each component must be >= 0. Intensity
/// is a dimensionless linear multiplier on the colour (an arbitrary linear
/// radiance scale) and must be >= 0. Directions are not required to be unit
/// length; they are normalised where used, and a zero-length direction is
/// rejected by validate(). Angles are in radians. Distances and radii are in
/// world units.
///
/// Every light validates through validate(), which throws
/// std::invalid_argument with a specific message on out-of-range or
/// degenerate input. Nothing out-of-range reaches a shader.

/// Directional light (the sun): direction the light travels in, colour and
/// intensity.
struct DirectionalLight
{
    core::vec3 direction{0.0f, 0.0f, -1.0f};  ///< travel direction, must be non-zero
    core::vec3 color{1.0f, 1.0f, 1.0f};       ///< linear RGB, components >= 0
    float intensity = 1.0f;                   ///< linear multiplier, >= 0

    void validate() const;
};

/// Point light with a documented attenuation model: windowed inverse square.
/// For a surface point at distance d (world units):
///
///   attenuation(d) = intensity * (1 - d / radius)^2 / (d^2 + 1)   for d <= radius
///   attenuation(d) = 0                                              for d > radius
///
/// (clamped smooth falloff to zero at the radius; intensity at d = 0).
struct PointLight
{
    core::vec3 position{0.0f, 0.0f, 0.0f};
    core::vec3 color{1.0f, 1.0f, 1.0f};  ///< linear RGB, components >= 0
    float intensity = 1.0f;              ///< >= 0
    float radius = 10.0f;                ///< range in world units, must be > 0
    /// Scattering density of the light's spherical volume, 1/world unit, >= 0.
    /// 0 (default) = not volumetric: the light shades surfaces only. > 0 makes
    /// the sphere of `radius` around the position additionally visible as
    /// in-scattered light (milestone 6f, decision 25 — a property of the
    /// point light, since the documentation does not say whether "volumetric"
    /// is a distinct type).
    float volumeDensity = 0.0f;

    void validate() const;
};

/// Spot light with a documented cone falloff: the cone factor is a smoothstep
/// between the outer and inner cone (cosine space), 1 inside the inner cone,
/// 0 outside the outer cone, combined with the same range attenuation as a
/// point light:
///
///   cone    = smoothstep(cos(outerAngle), cos(innerAngle), dot(-L, direction))
///   attenuation(d, cosAngle) = intensity * cone * (1 - d / range)^2 / (d^2 + 1)
///
/// Angles are in radians, innerAngle < outerAngle, both in (0, pi].
struct SpotLight
{
    core::vec3 position{0.0f, 0.0f, 0.0f};
    core::vec3 direction{0.0f, 0.0f, -1.0f};  ///< must be non-zero
    core::vec3 color{1.0f, 1.0f, 1.0f};       ///< linear RGB, components >= 0
    float intensity = 1.0f;                   ///< >= 0
    float range = 10.0f;                      ///< > 0, world units
    float innerAngle = 0.5f;                  ///< radians, in (0, outerAngle)
    float outerAngle = 1.0f;                  ///< radians, in (innerAngle, pi]
    /// Scattering density of the light's conical volume (truncated by `range`),
    /// 1/world unit, >= 0. 0 (default) = not volumetric; > 0 makes the cone
    /// additionally visible as in-scattered light (milestone 6f, decision 25).
    float volumeDensity = 0.0f;

    void validate() const;
};

/// A dynamic (per-object) light: point or spot. The directional sun is
/// handled separately in a submission.
using DynamicLight = std::variant<PointLight, SpotLight>;

/// Validates a dynamic light (delegates to the active alternative).
void validateLight(const DynamicLight& light);

/// Point-light range attenuation at distance d (documented model above).
float pointAttenuation(const PointLight& light, float distance);

/// Spot cone factor for cosAngle = dot(-L, spotDirection), both normalised.
float spotConeFactor(const SpotLight& light, float cosAngle);

/// Spot range+cone attenuation at distance d and cone cosine cosAngle.
float spotAttenuation(const SpotLight& light, float distance, float cosAngle);

}  // namespace grace::render
