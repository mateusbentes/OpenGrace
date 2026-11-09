#pragma once
// The video module's data types: the video descriptors and the playback
// state (docs/grace-architecture.md section 2's module table).
//
// The documented architecture keeps the video data as plain data: the
// application prepares the decoded frame data, the backend turns it
// into an internal handle, and the playback description carries the
// playback properties. The backend never owns application state, and
// every operation is a plain function call on a handle - no callbacks,
// no hidden threads (the stepping happens inside the backend's update).
//
// The documented architecture names the video service a "Playback
// service" without a codec. The open codec is Ogg Theora: it is the
// same ecosystem as the audio module's OpenAL Soft and Ogg Vorbis, and
// the decode happens in the data layer (a decoded frame is planar
// YCbCr data), never in the backend calls, which see only the frames.
#include <cstdint>
#include <string>
#include <vector>
namespace grace {
namespace video {
/// The pixel format of a decoded frame. The decoder produces planar
/// YCbCr at the file's own resolution; the conversion to the RGB the
/// renderer samples is a data-layer step, not a backend call.
enum class FrameFormat : std::uint8_t
{
    Yuv420,  ///< three planes, chroma at half the luma resolution
    Rgba8,   ///< four interleaved channels, 8 bits each
};
/// The playback state of a video.
enum class PlaybackState : std::uint8_t
{
    Stopped,  ///< the playback is not playing and is not queued
    Playing,  ///< the playback is producing frames
    Paused,   ///< the playback holds its cursor and is not producing
};
/// A decoded video: the frame data the application prepares once and
/// the backend uploads, plus the frame rate and the frame count the
/// file's headers declare. The bytes follow the format declared in the
/// descriptor.
struct VideoDesc
{
    std::vector<std::uint8_t> frames;  ///< the packed frame payload
    FrameFormat format = FrameFormat::Yuv420;
    std::uint32_t width = 0;   ///< texels, even (the 420 chroma subsamples)
    std::uint32_t height = 0;  ///< texels, even
    std::uint32_t frameRate = 0;  ///< frames per second, positive
    std::uint32_t frameCount = 0;  ///< packed frames in the payload
};
/// A playback description: one video instance and its playback
/// properties. The rate is a multiplier (1.0 is the file's own rate).
struct PlaybackDesc
{
    std::uint32_t videoId = 0;  ///< the id returned by the backend
    bool loop = false;  ///< restart the video when it ends
    float rate = 1.0f;  ///< rate multiplier, positive
};
/// The video module's statistics.
struct VideoStats
{
    std::uint32_t videoCount = 0;
    std::uint32_t playbackCount = 0;
    std::uint64_t framesPresented = 0;
    std::uint64_t violationCount = 0;
};
/// Validates a video descriptor. Returns the empty string when valid,
/// or the reason when invalid: zero size, an odd size, a zero rate, an
/// empty payload, or a payload that does not match the frame count.
std::string validate(const VideoDesc& desc);
/// Validates a playback descriptor. Returns the empty string when
/// valid, or the reason when invalid: a zero video id, a non-positive
/// rate.
std::string validate(const PlaybackDesc& desc);
/// The byte count of one packed frame in the format.
std::uint32_t frameBytes(FrameFormat format, std::uint32_t width,
                         std::uint32_t height);
}  // namespace video
}  // namespace grace
