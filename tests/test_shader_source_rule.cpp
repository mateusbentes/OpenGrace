// G0/G2 (decision 14): a real check that no shading-language source appears
// outside the OpenGL backend's shader library. Scans the source tree given as
// argv[1] for .hpp/.cpp files containing a shading-language marker ("#version")
// outside src/render/opengl/ and tests/, prints every hit, and fails if any.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "test_support.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <source-root>\n", argv[0]);
        return 2;
    }
    const fs::path root = argv[1];
    int filesScanned = 0;
    int hits = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const auto& path = entry.path();
        const std::string ext = path.extension().string();
        if (ext != ".hpp" && ext != ".cpp")
            continue;
        const std::string generic = fs::path(path).generic_string();
        // The OpenGL backend owns all shader sources; tests use ids only.
        if (generic.find("/src/render/opengl/") != std::string::npos)
            continue;
        if (generic.find("/tests/") != std::string::npos)
            continue;
        if (generic.find("/build") != std::string::npos)
            continue;
        ++filesScanned;
        std::ifstream in(path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        if (text.find("#version") != std::string::npos)
        {
            ++hits;
            std::printf("HIT: shading-language source in API-neutral file: %s\n",
                        generic.c_str());
        }
    }
    std::printf("shader-source rule: scanned %d files, %d hits outside src/render/opengl/ and tests/\n",
                filesScanned, hits);
    GRACE_CHECK_EQ(hits, 0);
    return grace::test::summary("shader_source_rule");
}
