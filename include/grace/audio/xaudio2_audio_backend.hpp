#pragma once
// The XAudio2 audio backend: the contract forwarded to the XAudio2.8
// mixer (xaudio2.h). Built only when the SDK option is on
// (GRACE_ENABLE_XAUDIO2) and only on Windows; the SDK lives outside the
// repository.
//
// The decode never happens in the backend calls: the WAV and Ogg Vorbis
// decoders (13c) live in the data layer and hand the backend PCM samples
// (architecture section 3.10). The adapter's job is the mapping only:
// the buffer ids to source voices, the stream chunks to submitted
// buffers, the 3D properties to the X3DAudio emitter, and the reverb
// assignment to the voice's record.
#include <memory>
#include "grace/audio/audio_backend.hpp"
namespace grace {
namespace audio {
/// XAudio2 audio backend: the contract forwarded to the XAudio2.8
/// mixer. The constructor initialises the engine and the mastering
/// voice; failures are reported through describe() and the violation
/// counter.
class XAudio2AudioBackend : public AudioBackend
{
public:
    XAudio2AudioBackend();
    ~XAudio2AudioBackend() override;
    XAudio2AudioBackend(const XAudio2AudioBackend&) = delete;
    XAudio2AudioBackend& operator=(const XAudio2AudioBackend&) = delete;
    XAudio2AudioBackend(XAudio2AudioBackend&&) = delete;
    XAudio2AudioBackend& operator=(XAudio2AudioBackend&&) = delete;
    std::string describe() const override;
    std::uint32_t capabilityFlags() const override;
    void shutdown() override;
    std::uint64_t violationCount() const override;
    std::uint32_t createBuffer(const SoundDesc& desc) override;
    void releaseBuffer(std::uint32_t bufferId) override;
    std::uint32_t bufferCount() const override;
    std::uint32_t createVoice(const VoiceDesc& desc) override;
    void releaseVoice(std::uint32_t voiceId) override;
    std::uint32_t voiceCount() const override;
    void play(std::uint32_t voiceId) override;
    void pause(std::uint32_t voiceId) override;
    void stop(std::uint32_t voiceId) override;
    VoiceState state(std::uint32_t voiceId) const override;
    void updateVoice(std::uint32_t voiceId, const VoiceDesc& desc) override;
    std::uint32_t cursor(std::uint32_t voiceId) const override;
    void setReverb(std::uint32_t voiceId, const ReverbDesc& desc) override;
    void clearReverb(std::uint32_t voiceId) override;
    bool reverb(std::uint32_t voiceId, ReverbDesc& out) const override;
    std::uint32_t openStream(const StreamDesc& desc) override;
    void releaseStream(std::uint32_t streamId) override;
    bool appendChunk(std::uint32_t streamId,
                     const std::vector<std::uint8_t>& chunk) override;
    std::uint32_t queuedFrames(std::uint32_t streamId) const override;
    std::uint32_t createStreamVoice(std::uint32_t streamId,
                                    const VoiceDesc& desc) override;
    void updateListener(const ListenerDesc& desc) override;
    void mix(std::uint32_t frames) override;
    AudioStats stats() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace audio
}  // namespace grace
