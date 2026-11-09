// The video module's decode layer: the validation of the decoded
// payload against the backend's rules. The decode itself lives with the
// adapter's sources (the SDK option).
#include "grace/video/video_decode.hpp"
#include "grace/video/video_types.hpp"
namespace grace {
namespace video {
std::string validate(const Decoded& decoded)
{
    if (!decoded.ok)
    {
        return "the decode failed";
    }
    if (decoded.width == 0u || decoded.height == 0u)
    {
        return "the decoded size is zero";
    }
    if (decoded.frameRate == 0u)
    {
        return "the decoded frame rate is zero";
    }
    if (decoded.frameCount == 0u)
    {
        return "the decoded frame count is zero";
    }
    const std::uint32_t bytes = frameBytes(FrameFormat::Yuv420,
                                           decoded.width, decoded.height);
    if (decoded.frames.size() != bytes * decoded.frameCount)
    {
        return "the payload does not match its frame count";
    }
    return "";
}
}  // namespace video
}  // namespace grace
