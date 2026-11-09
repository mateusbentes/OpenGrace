#pragma once
// The audio module's contract: the backend interface the application
// programs against (docs/grace-architecture.md section 3.10). One class,
// plain data in and out, never throws.
//
// The contract is the protocol reference for every backend: the null
// backend (13b) implements it with a deterministic mixer, and the OpenAL
// Soft adapter (13c) forwards the calls. The unit of playback is a
// voice; the unit of data is a buffer. A stream is a buffer whose chunks
// the application appends as it decodes them.
#include <cstdint>
#include <string>
#include "grace/audio/audio_types.hpp"
namespace grace {
namespace audio {
/// Opaque buffer handle. The backend owns the objects; the application
/// moves the handles.
class SoundBuffer;
/// Opaque voice handle. The backend owns the objects; the application
/// moves the handles.
class Voice;
/// Opaque stream handle. The backend owns the objects; the application
/// moves the handles.
class Stream;
/// The audio backend contract: upload buffers, drive voices, mix in the
/// update. Every call is safe on a null or foreign handle; the misuses
/// bump a violation counter the application can read.
class AudioBackend
{
public:
    AudioBackend() = default;
    virtual ~AudioBackend() = default;
    AudioBackend(const AudioBackend&) = delete;
    AudioBackend& operator=(const AudioBackend&) = delete;
    AudioBackend(AudioBackend&&) = delete;
    AudioBackend& operator=(AudioBackend&&) = delete;
    /// A human-readable description of the backend, for diagnostics.
    virtual std::string describe() const = 0;
    /// The backend capability flags: 0 means the plain contract. A
    /// backend may set bits for extra features (EFX reverb, for example).
    virtual std::uint32_t capabilityFlags() const = 0;
    /// Releases every backend resource. Idempotent.
    virtual void shutdown() = 0;
    /// The number of protocol violations since the start: the misuses
    /// the application can detect (a null handle, a foreign handle).
    virtual std::uint64_t violationCount() const = 0;
    // -- Buffers ----------------------------------------
    //
    // A buffer is a decoded PCM payload the application uploads once.
    /// Uploads a sound descriptor. Returns the buffer id (1-based) for the
    /// application to reference it; 0 on an invalid descriptor.
    virtual std::uint32_t createBuffer(const SoundDesc& desc) = 0;
    /// Releases a buffer. Releasing an unknown id is a protocol violation.
    virtual void releaseBuffer(std::uint32_t bufferId) = 0;
    /// The buffer count currently alive.
    virtual std::uint32_t bufferCount() const = 0;
    // -- Voices ----------------------------------------
    //
    // A voice is one playback instance of one buffer. The voice carries
    // the gain, the pitch, the loop and the 3D properties.
    /// Creates a voice from a buffer id. Returns the voice id (1-based);
    /// 0 on an unknown buffer id.
    virtual std::uint32_t createVoice(const VoiceDesc& desc) = 0;
    /// Releases a voice. Releasing an unknown id is a protocol violation.
    virtual void releaseVoice(std::uint32_t voiceId) = 0;
    /// The voice count currently alive.
    virtual std::uint32_t voiceCount() const = 0;
    /// Starts a voice. Starting a playing voice rewinds it.
    virtual void play(std::uint32_t voiceId) = 0;
    /// Pauses a voice. The cursor holds; a paused voice is not audible.
    virtual void pause(std::uint32_t voiceId) = 0;
    /// Stops a voice. The cursor resets; the voice is not queued.
    virtual void stop(std::uint32_t voiceId) = 0;
    /// The state of a voice. Stopped on an unknown id.
    virtual VoiceState state(std::uint32_t voiceId) const = 0;
    /// Updates the voice's properties: gain, pitch, loop and the 3D
    /// properties. An unknown id is a protocol violation.
    virtual void updateVoice(std::uint32_t voiceId, const VoiceDesc& desc) = 0;
    /// The current playback cursor of a voice in frames. A looping voice
    /// reports the cursor inside the buffer.
    virtual std::uint32_t cursor(std::uint32_t voiceId) const = 0;
    // -- Reverb ----------------------------------------
    //
    // Per-sound reverb (architecture section 3.10): the zone is a
    // property the application assigns to a voice. The backend mixes the
    // dry signal with the wet one through the voice's reverb.
    /// Assigns a reverb zone to a voice. A null zone id removes the
    /// assignment. An unknown voice id is a protocol violation.
    virtual void setReverb(std::uint32_t voiceId,
                           const ReverbDesc& desc) = 0;
    /// Removes the reverb zone from a voice.
    virtual void clearReverb(std::uint32_t voiceId) = 0;
    /// The reverb assignment of a voice. Returns false when the voice
    /// has none.
    virtual bool reverb(std::uint32_t voiceId, ReverbDesc& out) const = 0;
    // -- Streams ----------------------------------------
    //
    // A stream is a buffer whose chunks the application appends as it
    // decodes them: the streaming path for the long sounds.
    /// Opens a stream. Returns the stream id (1-based); 0 on an invalid
    /// descriptor.
    virtual std::uint32_t openStream(const StreamDesc& desc) = 0;
    /// Releases a stream. Releasing an unknown id is a protocol violation.
    virtual void releaseStream(std::uint32_t streamId) = 0;
    /// Appends a decoded chunk to a stream. The chunk is PCM bytes in
    /// the stream's format. Returns false when the stream is unknown.
    virtual bool appendChunk(std::uint32_t streamId,
                             const std::vector<std::uint8_t>& chunk) = 0;
    /// The number of frames currently queued in a stream.
    virtual std::uint32_t queuedFrames(std::uint32_t streamId) const = 0;
    /// Creates a voice from a stream id. The stream plays in order; the
    /// voice loops only when the application re-appends the chunks.
    /// Returns the voice id (1-based); 0 on an unknown stream id.
    virtual std::uint32_t createStreamVoice(std::uint32_t streamId,
                                            const VoiceDesc& desc) = 0;
    // -- Listener ----------------------------------------
    /// Updates the 3D listener: the frame of reference for the 3D voices.
    virtual void updateListener(const ListenerDesc& desc) = 0;
    // -- Mixing ----------------------------------------
    /// Mixes the next \p frames frames of audio. The null backend
    /// accumulates the deterministic sum; the OpenAL adapter forwards the
    /// call to the device's context. The listener and the voice updates
    /// take effect on the next mix.
    virtual void mix(std::uint32_t frames) = 0;
    /// The statistics the backend reports.
    virtual AudioStats stats() const = 0;
};
}  // namespace audio
}  // namespace grace
