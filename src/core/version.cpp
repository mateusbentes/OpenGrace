#include "grace/core/version.hpp"

namespace grace::core {

std::string versionString()
{
    const Version v = engineVersion();
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." +
           std::to_string(v.patch);
}

}  // namespace grace::core
