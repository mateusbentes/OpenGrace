#include "grace/physics/physics_types.hpp"

#include <cmath>
#include <cstdio>

namespace grace::physics {
namespace {

std::string invalid(const char* what, const char* detail)
{
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%s: %s", what, detail);
    return std::string(buffer);
}

bool isFinite(const core::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool allPositive(const core::vec3& v)
{
    return v.x > 0.0f && v.y > 0.0f && v.z > 0.0f;
}

}  // namespace

bool ShapeDesc::validate(std::string& message) const
{
    switch (kind)
    {
    case Kind::Box:
        if (!isFinite(boxHalfExtents) || !allPositive(boxHalfExtents))
        {
            message = invalid("box shape", "half extents must be finite and > 0");
            return false;
        }
        break;
    case Kind::Sphere:
        if (!std::isfinite(sphereRadius) || sphereRadius <= 0.0f)
        {
            message = invalid("sphere shape", "radius must be finite and > 0");
            return false;
        }
        break;
    case Kind::Capsule:
        if (!std::isfinite(capsuleRadius) || capsuleRadius <= 0.0f)
        {
            message = invalid("capsule shape", "radius must be finite and > 0");
            return false;
        }
        if (!std::isfinite(capsuleHalfHeight) || capsuleHalfHeight <= 0.0f)
        {
            message = invalid("capsule shape",
                              "half height must be finite and > 0");
            return false;
        }
        break;
    }
    return true;
}

bool BodyDesc::validate(std::string& message) const
{
    if (!shape.validate(message))
    {
        message = invalid("body", message.c_str());
        return false;
    }
    if (!isFinite(position))
    {
        message = invalid("body", "position must be finite");
        return false;
    }
    if (kind == BodyKind::Dynamic)
    {
        if (!std::isfinite(massKg) || massKg <= 0.0f)
        {
            message = invalid("dynamic body", "mass must be finite and > 0");
            return false;
        }
        if (!std::isfinite(linearDamping) || linearDamping < 0.0f)
        {
            message = invalid("dynamic body", "linear damping must be >= 0");
            return false;
        }
        if (!std::isfinite(angularDamping) || angularDamping < 0.0f)
        {
            message = invalid("dynamic body", "angular damping must be >= 0");
            return false;
        }
    }
    return true;
}

bool JointDesc::validate(std::string& message) const
{
    switch (kind)
    {
    case Kind::Distance:
        if (!std::isfinite(restLength) || restLength < 0.0f)
        {
            message = invalid("distance joint",
                              "rest length must be finite and >= 0");
            return false;
        }
        break;
    case Kind::PointToPoint:
        break;
    }
    if (!isFinite(anchorA) || !isFinite(anchorB))
    {
        message = invalid("joint", "anchors must be finite");
        return false;
    }
    return true;
}

bool BuoyancyVolumeDesc::validate(std::string& message) const
{
    if (!isFinite(extents) || !allPositive(extents))
    {
        message = invalid("buoyancy volume", "extents must be finite and > 0");
        return false;
    }
    if (!std::isfinite(densityKgPerM3) || densityKgPerM3 <= 0.0f)
    {
        message = invalid("buoyancy volume", "density must be finite and > 0");
        return false;
    }
    if (!std::isfinite(drag) || drag < 0.0f)
    {
        message = invalid("buoyancy volume", "drag must be finite and >= 0");
        return false;
    }
    return true;
}

bool ForceFieldDesc::validate(std::string& message) const
{
    if (!isFinite(extents) || !allPositive(extents))
    {
        message = invalid("force field", "extents must be finite and > 0");
        return false;
    }
    if (!isFinite(acceleration))
    {
        message = invalid("force field", "acceleration must be finite");
        return false;
    }
    return true;
}

bool WorldConfig::validate(std::string& message) const
{
    if (!isFinite(gravity))
    {
        message = invalid("world", "gravity must be finite");
        return false;
    }
    if (!std::isfinite(fixedStepSeconds) || fixedStepSeconds <= 0.0f)
    {
        message = invalid("world",
                          "fixed step must be finite and > 0 seconds");
        return false;
    }
    return true;
}

}  // namespace grace::physics
