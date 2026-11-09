#pragma once
#include <memory>
#include "grace/audio/audio_backend.hpp"
namespace grace {
namespace audio {
/// Null audio backend: a headless implementation of the contract with a
/// deterministic mixer. It exists so that deterministic tests can run
/// without an audio SDK linked, and so that application code can be
/// written against the contract before a real backend lands.
///
/// The mixing is deterministic: the mix() call accumulates the sum of
/// the audible voices' contributions into a counter, with the gain and
/// the pitch applied as the contract declares them. The 3D voices
/// attenuate by the inverse-distance model the contract declares. No
/// real device is touched, so every run is reproducible bit-for-bit.
class NullAudioBackend : public AudioBackend
{
public:
    NullAudioBackend();
    ~NullAudioBackend() override;
    NullAudioBackend(const NullAudioBackend&) = delete;
    NullAudioBackend& operator=(const NullAudioBackend&) = delete;
    NullAudioBackend(NullAudioBackend&&) = delete;
    NullAudioBackend& operator=(NullAudioBackend&&) = delete;
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
