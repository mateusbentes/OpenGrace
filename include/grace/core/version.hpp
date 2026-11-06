#pragma once

#include <string>

namespace grace::core {

/// Engine version, independent of the file-format version.
struct Version
{
    int major;
    int minor;
    int patch;
};

/// Compile-time version of the engine.
constexpr Version engineVersion() noexcept
{
    return Version{0, 1, 0};
}

/// Human-readable version, for example "0.1.0".
std::string versionString();

}  // namespace grace::core
