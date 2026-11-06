#include "grace/core/version.hpp"

#include <string>

#include "test_support.hpp"

int main()
{
    using grace::core::engineVersion;
    using grace::core::versionString;

    GRACE_CHECK_EQ(engineVersion().major, 0);
    GRACE_CHECK_EQ(engineVersion().minor, 1);
    GRACE_CHECK_EQ(engineVersion().patch, 0);

    GRACE_CHECK_EQ(versionString(), std::string("0.1.0"));

    // The accessor must stay constexpr so that version checks can be resolved
    // at compile time by dependent modules.
    constexpr auto kCompileTimeVersion = engineVersion();
    static_assert(kCompileTimeVersion.major == 0, "major version mismatch");

    return grace::test::summary("version");
}
