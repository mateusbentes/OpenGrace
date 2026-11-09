// The video module's data types: the validation of the video and
// playback descriptions.
#include "grace/video/video_types.hpp"
namespace grace {
namespace video {
std::string validate(const VideoDesc& desc)
{
    if (desc.width == 0u || desc.height == 0u)
    {
        return "a video's size is zero";
    }
    if (desc.width % 2u != 0u || desc.height % 2u != 0u)
    {
        return "a video's size is not even (the 420 chroma subsamples)";
    }
    if (desc.frameRate == 0u)
    {
        return "a video's frame rate is zero";
    }
    if (desc.frameCount == 0u)
    {
        return "a video's frame count is zero";
    }
    if (desc.frames.empty())
    {
        return "a video's payload is empty";
    }
    const std::uint32_t bytes = frameBytes(desc.format, desc.width,
                                           desc.height);
    if (desc.frames.size() != bytes * desc.frameCount)
    {
        return "a video's payload does not match its frame count";
    }
    return "";
}
std::string validate(const PlaybackDesc& desc)
{
    if (desc.videoId == 0u)
    {
        return "a playback's video id is zero";
    }
    if (desc.rate <= 0.0f)
    {
        return "a playback's rate is not positive";
    }
    return "";
}
std::uint32_t frameBytes(FrameFormat format, std::uint32_t width,
                         std::uint32_t height)
{
    const std::uint32_t pixels = width * height;
    switch (format)
    {
    case FrameFormat::Yuv420:
        // The luma plane is one byte per pixel; the chroma planes are
        // one byte per 2x2 block, so a quarter each.
        return pixels + pixels / 4u + pixels / 4u;
    case FrameFormat::Rgba8:
        return pixels * 4u;
    }
    return 0u;
}
}  // namespace video
}  // namespace grace
