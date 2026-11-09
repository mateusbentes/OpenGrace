#pragma once
// The audio module's data types: buffers, voices, reverb zones, the 3D
// listener and the streaming descriptors (docs/grace-architecture.md
// section 3.10).
//
// The documented architecture keeps the audio data as plain data: the
// application prepares the sample data, the backend turns it into an
// internal handle, and the voice description carries the playback
// properties. The backend never owns application state, and every
// operation is a plain function call on a handle - no callbacks, no
// hidden threads (the mixing happens inside the backend's update).
//
// Supported formats are Ogg Vorbis and WAV. The decode happens in the
// data layer (a decoded buffer is PCM sample data), never in the
// backend calls, which see only the PCM samples.
#include <cstdint>
#include <string>
#include <vector>
namespace grace {
namespace audio {
/// The sample format of a buffer's PCM data.
enum class SampleFormat : std::uint8_t
{
    Mono8,     ///< one unsigned 8-bit channel
    Mono16,    ///< one signed 16-bit little-endian channel
    Stereo8,   ///< two unsigned 8-bit channels
    Stereo16,  ///< two signed 16-bit little-endian channels
};
/// How a voice positions itself in the mix.
enum class VoiceMode : std::uint8_t
{
    Sound2D,  ///< no distance attenuation; the listener is ignored
    Sound3D,  ///< distance attenuation and panning from the listener
};
/// The playback state of a voice.
enum class VoiceState : std::uint8_t
{
    Stopped,  ///< the voice is not playing and is not queued
    Playing,  ///< the voice is audible
    Paused,   ///< the voice holds its cursor and is not audible
};
/// A decoded PCM buffer: the sample data the application prepares once
/// and the backend uploads. The bytes follow the format and the sample
/// rate declared in the descriptor.
struct SoundDesc
{
    std::vector<std::uint8_t> samples;  ///< the PCM sample bytes
    SampleFormat format = SampleFormat::Mono16;
    std::uint32_t sampleRate = 44100;  ///< samples per second per channel
};
/// A voice description: one buffer instance and its playback properties.
/// The gain is linear (1.0 is unity), the pitch a rate multiplier.
struct VoiceDesc
{
    std::uint32_t bufferId = 0;  ///< the id returned by the backend
    VoiceMode mode = VoiceMode::Sound2D;
    float gain = 1.0f;          ///< linear gain, 0.0 to 1.0
    float pitch = 1.0f;         ///< rate multiplier, positive
    bool loop = false;          ///< restart the buffer when it ends
    float position[3] = {0.0f, 0.0f, 0.0f};  ///< world position (3D mode)
    float referenceDistance = 1.0f;  ///< the distance where gain reaches 1.0
    float maxDistance = 100.0f;      ///< the distance where gain reaches 0.0
};
/// A reverb zone description. Per-sound reverb (architecture section
/// 3.10) means the zone is a property the application assigns to a voice;
/// the backend mixes the dry signal with the wet one.
struct ReverbDesc
{
    float decay = 1.0f;        ///< seconds, the decay time of the tail
    float damping = 0.5f;      ///< 0.0 to 1.0, how much the tail is muted
    float wetGain = 0.3f;      ///< linear gain of the wet mix
};
/// The 3D listener: one per backend, the frame of reference for the 3D
/// voices. The orientation is two vectors: forward and up.
struct ListenerDesc
{
    float position[3] = {0.0f, 0.0f, 0.0f};
    float forward[3] = {0.0f, 0.0f, -1.0f};
    float up[3] = {0.0f, 1.0f, 0.0f};
    float gain = 1.0f;  ///< master gain, 0.0 to 1.0
};
/// A streaming descriptor: a stream is a buffer that is filled in chunks
/// as the application decodes them. The descriptor carries the format and
/// the chunk size; the application appends chunks through the backend.
struct StreamDesc
{
    SampleFormat format = SampleFormat::Mono16;
    std::uint32_t sampleRate = 44100;
    std::uint32_t chunkSamples = 8192;  ///< samples per channel per chunk
};
/// Statistics the backend reports: the counters the deterministic tests
/// and the application can read.
struct AudioStats
{
    std::uint32_t bufferCount = 0;  ///< buffers currently uploaded
    std::uint32_t voiceCount = 0;   ///< voices currently alive
    std::uint32_t playingCount = 0; ///< voices currently playing
    std::uint32_t streamCount = 0;  ///< streams currently open
    std::uint64_t samplesMixed = 0; ///< samples per channel mixed since start
};
/// The validation of a sound descriptor: a non-empty payload, a non-zero
/// sample rate and a byte count that matches the format. Returns the
/// empty string when the descriptor is valid, or the reason.
std::string validate(const SoundDesc& desc);
/// The validation of a voice descriptor: a non-zero gain, a positive
/// pitch, a positive reference distance and a max distance not below the
/// reference one. Returns the empty string when valid.
std::string validate(const VoiceDesc& desc);
/// The validation of a reverb descriptor: non-negative decay and a
/// damping and a wet gain inside the unit range. Returns the empty
/// string when valid.
std::string validate(const ReverbDesc& desc);
/// The validation of a stream descriptor: a non-zero sample rate and a
/// non-zero chunk size. Returns the empty string when valid.
std::string validate(const StreamDesc& desc);
/// The number of channels of a sample format.
std::uint32_t channelCount(SampleFormat format);
/// The number of bytes per frame of a sample format: one frame is one
/// sample per channel.
std::uint32_t bytesPerFrame(SampleFormat format);
/// The frame count of a PCM payload: the byte count divided by the frame
/// size. Returns 0 when the payload does not divide evenly.
std::uint32_t frameCount(const std::vector<std::uint8_t>& samples,
                         SampleFormat format);
}  // namespace audio
}  // namespace grace
