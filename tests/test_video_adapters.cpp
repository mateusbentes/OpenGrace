// The video adapter suite (16c): the Theora decode layer. The suite
// tests the decode's rejections and, when a system Theora file exists,
// the decode of the real file. The file path is the environment's
// override, so the suite can run without one.
#include "contract_checks.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "grace/video/video_decode.hpp"
#include "grace/video/video_types.hpp"
namespace {
using grace::video::Decoded;
using grace::video::decodeTheora;
/// Reads a file into the byte span. Returns the empty vector when the
/// file does not exist.
std::vector<std::uint8_t> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return {};
    }
    return std::vector<std::uint8_t>(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
}
void testDecodeRejections()
{
    // A non-Ogg payload: the decode rejects it.
    const std::vector<std::uint8_t> notOgg = {'N', 'O', 'P', 'E'};
    const Decoded failed = decodeTheora(notOgg);
    GRACE_CHECK(!failed.ok);  // "a non-Ogg payload is rejected
    // "the reason is the format
    GRACE_CHECK(failed.reason == "not an Ogg container");
    // An Ogg container that is not Theora: the decode rejects it.
    const std::vector<std::uint8_t> oggButNotTheora = {
        'O', 'g', 'g', 'S', 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x7Eu};
    const Decoded failed2 = decodeTheora(oggButNotTheora);
    GRACE_CHECK(!failed2.ok);  // "a non-Theora Ogg payload is rejected
}
void testDecodeRealFile(const std::string& path)
{
    const std::vector<std::uint8_t> bytes = readFile(path);
    if (bytes.size() < 4u)
    {
        return;
    }
    const Decoded decoded = decodeTheora(bytes);
    GRACE_CHECK(decoded.ok);  // "the real file decodes
    GRACE_CHECK(decoded.reason.empty());  // "the reason is empty
    GRACE_CHECK(decoded.width > 0u);  // "the width is positive
    GRACE_CHECK(decoded.height > 0u);  // "the height is positive
    GRACE_CHECK(decoded.frameRate > 0u);  // "the frame rate is positive
    GRACE_CHECK(decoded.frameCount > 0u);  // "the frame count is positive
    // The payload matches the frame count.
    const std::uint32_t frameBytes = grace::video::frameBytes(
        grace::video::FrameFormat::Yuv420, decoded.width, decoded.height);
    GRACE_CHECK(decoded.frames.size() == frameBytes * decoded.frameCount);
    // The validation of the decoded payload passes.
    GRACE_CHECK(grace::video::validate(decoded).empty());
}
}  // namespace
int main()
{
    testDecodeRejections();
    // The real file: the environment's override, or the known path.
    if (const char* override = std::getenv("GRACE_TEST_THEORA"))
    {
        testDecodeRealFile(override);
    }
    else
    {
        testDecodeRealFile("/usr/share/opengrace/tests/theora-test.ogg");
    }
    return grace::test::summary("video_adapters");
}
