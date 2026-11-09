#pragma once
// The audio module's decode layer: the WAV and Ogg Vorbis decoders that
// turn file bytes into the PCM data the backend uploads. The decode
// happens in the data layer, never in the backend calls (architecture
// section 3.10): the backend sees only the PCM samples.
#include <cstdint>
#include <string>
#include <vector>
#include "grace/audio/audio_types.hpp"
namespace grace {
namespace audio {
/// The result of a decode: the PCM payload, or the reason when the
/// decode failed. The reason never throws; the caller checks the flag.
struct Decoded
{
    bool ok = false;
    std::string reason;  ///< empty when the decode succeeded
    std::vector<std::uint8_t> samples;  ///< the PCM payload
    SampleFormat format = SampleFormat::Mono16;
    std::uint32_t sampleRate = 0;
};
/// Decodes a WAV file: the RIFF header, the fmt chunk and the data
/// chunk. Supports 8-bit mono/stereo and 16-bit little-endian
/// mono/stereo. Rejects the IEEE float and the planar formats.
Decoded decodeWav(const std::vector<std::uint8_t>& bytes);
/// Decodes an Ogg Vorbis file: the identification and the setup headers
/// through libvorbisfile, then the whole stream into the PCM payload.
/// The sample rate and the channel count come from the file's headers.
Decoded decodeVorbis(const std::vector<std::uint8_t>& bytes);
/// The validation of a decoded payload against the backend's rules: a
/// non-empty payload, a non-zero sample rate and a byte count that
/// matches the format. Returns the empty string when valid.
std::string validate(const Decoded& decoded);
}  // namespace audio
}  // namespace grace
