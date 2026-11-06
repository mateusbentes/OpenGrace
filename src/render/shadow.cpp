#include "grace/render/shadow.hpp"

#include <cmath>
#include <stdexcept>

#include "grace/core/math/camera.hpp"

namespace grace::render {

core::mat4 sunLightSpaceMatrix(const DirectionalLight& sun, const SceneBounds& bounds,
                               float near, float far)
{
    sun.validate();  // rejects a zero-length direction with a clear message
    if (!(bounds.radius > 0.0f))
        throw std::invalid_argument("shadow: scene bounds radius must be > 0");
    if (!(near >= 0.0f))
        throw std::invalid_argument("shadow: near must be >= 0");
    if (!(far > near))
        throw std::invalid_argument("shadow: far must be > near");
    const core::vec3 dir = core::normalise(sun.direction);
    // Eye placed so the near plane touches the light-side extreme of the
    // bounding sphere; every scene point is then in front of the camera.
    const core::vec3 eye = bounds.center - dir * (near + bounds.radius);
    // Pick an up vector that is not parallel to the light direction (lookAt
    // would otherwise be degenerate).
    const core::vec3 up =
        std::fabs(core::dot(dir, core::vec3{0.0f, 1.0f, 0.0f})) > 0.99f
            ? core::vec3{1.0f, 0.0f, 0.0f}
            : core::vec3{0.0f, 1.0f, 0.0f};
    const core::mat4 view = core::lookAt(eye, bounds.center, up);
    const core::mat4 proj = core::orthographic(-bounds.radius, bounds.radius,
                                               -bounds.radius, bounds.radius, near, far);
    return proj * view;
}

}  // namespace grace::render
