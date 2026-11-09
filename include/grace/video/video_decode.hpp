#pragma once
// The video module's decode layer: the Ogg Theora decoder that turns
// file bytes into the packed frame payload the backend uploads. The
// decode happens in the data layer, never in the backend calls (the
// same rule the audio module's decode layer follows): the backend sees
// only the frames.
#include <cstdint>
#include <string>
#include <vector>
#include "grace/video/video_types.hpp"
namespace grace {
namespace video {
/// The result of a decode: the frame payload, or the reason when the
/// decode failed. The reason never throws; the caller checks the flag.
struct Decoded
{
    bool ok = false;
    std::string reason;  ///< empty when the decode succeeded
    std::vector<std::uint8_t> frames;  ///< the packed frame payload
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frameRate = 0;
    std::uint32_t frameCount = 0;
};
/// Decodes an Ogg Theora file: the identification and the comment
/// headers, then every frame into the packed payload. The frame rate
/// and the frame count come from the file's headers.
Decoded decodeTheora(const std::vector<std::uint8_t>& bytes);
/// The validation of a decoded payload against the backend's rules: a
/// non-empty payload, an even size, a non-zero rate and a byte count
/// that matches the frame count. Returns the empty string when valid.
std::string validate(const Decoded& decoded);
}  // namespace video
}  // namespace grace
