#pragma once

#include "grace/core/math/vec3.hpp"

namespace grace::render {

/// Camera parameters, API-neutral. fovYRad is the vertical field of view in
/// radians (documented convention, same as core::perspective). The default is
/// the backend's documented diagnostic camera: eye (0,0,1) looking at the
/// origin, up +Y, fovY 90 degrees.
struct CameraParams
{
    core::vec3 eye{0.0f, 0.0f, 1.0f};
    core::vec3 target{0.0f, 0.0f, 0.0f};
    core::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRad = 1.5707963267948966f;  ///< pi/2
    float aspect = 1.0f;                  ///< width / height, > 0
    float near = 0.1f;                    ///< near plane distance, > 0
    float far = 100.0f;                   ///< far plane distance, > near

    /// Throws std::invalid_argument on out-of-range or degenerate values.
    void validate() const;
};

}  // namespace grace::render
