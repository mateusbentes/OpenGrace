#include "grace/render/material.hpp"

#include <stdexcept>
#include <string>

namespace grace::render {

namespace {

void validateUnitRange(const core::vec3& color, const char* field)
{
    if (color.x < 0.0f || color.x > 1.0f || color.y < 0.0f || color.y > 1.0f ||
        color.z < 0.0f || color.z > 1.0f)
        throw std::invalid_argument(std::string("surface: ") + field +
                                    " components must be in [0, 1] (linear reflectance)");
}

}  // namespace

void validateSurfaceParams(const SurfaceParams& surface)
{
    validateUnitRange(surface.albedo, "albedo");
    validateUnitRange(surface.specular, "specular");
    if (!(surface.shininess >= 0.0f))
        throw std::invalid_argument("surface: shininess must be >= 0 (Blinn-Phong exponent)");
    if (surface.emissive.x < 0.0f || surface.emissive.y < 0.0f || surface.emissive.z < 0.0f)
        throw std::invalid_argument("surface: emissive components must be >= 0 (linear)");
}

void Material::setSurfaceParams(const SurfaceParams& surface)
{
    validateSurfaceParams(surface);
    m_surface = surface;
}

}  // namespace grace::render
