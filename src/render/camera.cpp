#include "grace/render/camera.hpp"

#include <cmath>
#include <stdexcept>

#include "grace/core/math/vec3.hpp"

namespace grace::render {

void CameraParams::validate() const
{
    if (!(fovYRad > 0.0f) || !(fovYRad < 3.14159265358979323846f))
        throw std::invalid_argument("camera: fovYRad must be in (0, pi) radians");
    if (!(aspect > 0.0f) || !std::isfinite(aspect))
        throw std::invalid_argument("camera: aspect must be a finite value > 0");
    if (!(near > 0.0f) || !std::isfinite(near))
        throw std::invalid_argument("camera: near must be a finite value > 0");
    if (!(far > near) || !std::isfinite(far))
        throw std::invalid_argument("camera: far must be finite and > near");
    if (eye == target)
        throw std::invalid_argument("camera: eye and target must differ");
    if (core::length(core::cross(core::normalise(target - eye), up)) == 0.0f)
        throw std::invalid_argument("camera: up vector parallel to the view direction");
}

}  // namespace grace::render
