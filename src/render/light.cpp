#include "grace/render/light.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace grace::render {

namespace {

void validateColor(const core::vec3& color, const char* lightKind)
{
    if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f)
        throw std::invalid_argument(std::string(lightKind) +
                                    " light: colour components must be >= 0 (linear RGB)");
}

void validateIntensity(float intensity, const char* lightKind)
{
    if (!(intensity >= 0.0f) || !std::isfinite(intensity))
        throw std::invalid_argument(std::string(lightKind) +
                                    " light: intensity must be a finite value >= 0");
}

void validateDirection(const core::vec3& direction, const char* lightKind)
{
    // Checked directly (not via normalise) so validation throws instead of
    // hitting the math contract-violation path.
    if (core::length(direction) == 0.0f)
        throw std::invalid_argument(std::string(lightKind) +
                                    " light: zero-length direction");
}

void validateRange(float range, const char* field, const char* lightKind)
{
    if (!(range > 0.0f) || !std::isfinite(range))
        throw std::invalid_argument(std::string(lightKind) + " light: " + field +
                                    " must be a finite value > 0");
}

}  // namespace

void DirectionalLight::validate() const
{
    validateDirection(direction, "directional");
    validateColor(color, "directional");
    validateIntensity(intensity, "directional");
}

void PointLight::validate() const
{
    validateColor(color, "point");
    validateIntensity(intensity, "point");
    validateRange(radius, "radius", "point");
    if (!(volumeDensity >= 0.0f))
        throw std::invalid_argument("point light: volumeDensity must be >= 0");
}

void SpotLight::validate() const
{
    validateDirection(direction, "spot");
    validateColor(color, "spot");
    validateIntensity(intensity, "spot");
    validateRange(range, "range", "spot");
    if (!(innerAngle > 0.0f) || !(outerAngle <= 3.14159265358979323846f))
        throw std::invalid_argument(
            "spot light: cone angles must be in radians with 0 < inner <= outer <= pi");
    if (!(innerAngle < outerAngle))
        throw std::invalid_argument(
            "spot light: inverted cone (innerAngle must be < outerAngle)");
    if (!(volumeDensity >= 0.0f))
        throw std::invalid_argument("spot light: volumeDensity must be >= 0");
}

void validateLight(const DynamicLight& light)
{
    std::visit([](const auto& alternative) { alternative.validate(); }, light);
}

float pointAttenuation(const PointLight& light, float distance)
{
    light.validate();
    if (distance > light.radius || distance < 0.0f)
        return 0.0f;
    const float window = 1.0f - distance / light.radius;
    return light.intensity * window * window / (distance * distance + 1.0f);
}

float spotConeFactor(const SpotLight& light, float cosAngle)
{
    light.validate();
    const float cosInner = std::cos(light.innerAngle);
    const float cosOuter = std::cos(light.outerAngle);
    // smoothstep between cos(outer) and cos(inner): 0 outside, 1 inside.
    const float t = (cosAngle - cosOuter) / (cosInner - cosOuter);
    const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

float spotAttenuation(const SpotLight& light, float distance, float cosAngle)
{
    if (distance > light.range || distance < 0.0f)
        return 0.0f;
    const float window = 1.0f - distance / light.range;
    return light.intensity * spotConeFactor(light, cosAngle) * window * window /
           (distance * distance + 1.0f);
}

}  // namespace grace::render
