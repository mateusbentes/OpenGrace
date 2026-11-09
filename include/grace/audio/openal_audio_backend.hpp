#pragma once
// The OpenAL Soft adapter: the contract forwarded to the device through
// the OpenAL Soft API. Built only when the SDK option is on
// (GRACE_ENABLE_OPENAL); the SDK lives outside the repository.
//
// The decode never happens in the backend calls: the WAV and Ogg Vorbis
// decoders (13c) live in the data layer and hand the backend PCM samples
// (architecture section 3.10). The adapter's job is the mapping only:
// the buffer ids to AL buffers, the voice ids to AL sources, the stream
// to queued AL buffers, and the mix to the device's context update.
#include <memory>
#include "grace/audio/audio_backend.hpp"
namespace grace {
namespace audio {
/// OpenAL Soft audio backend: the contract forwarded to the device. The
/// constructor initialises the default device; failures are reported
/// through describe() and the violation counter.
class OpenALAudioBackend : public AudioBackend
{
public:
    OpenALAudioBackend();
    ~OpenALAudioBackend() override;
    OpenALAudioBackend(const OpenALAudioBackend&) = delete;
    OpenALAudioBackend& operator=(const OpenALAudioBackend&) = delete;
    OpenALAudioBackend(OpenALAudioBackend&&) = delete;
    OpenALAudioBackend& operator=(OpenALAudioBackend&&) = delete;
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
