#pragma once
// The OpenSL ES audio backend: the contract forwarded to the Android
// OpenSL ES API (SLES/OpenSLES.h). Built only when the SDK option is on
// (GRACE_ENABLE_OPENSL) and only on Android; the SDK lives in the NDK.
//
// The decode never happens in the backend calls: the WAV and Ogg Vorbis
// decoders (13c) live in the data layer and hand the backend PCM samples
// (architecture section 3.10). The adapter's job is the mapping only:
// the buffer ids to player objects, the stream chunks to buffer-queue
// slots, the 3D properties to the 3DLocation interface, and the reverb
// assignment to the voice's record.
#include <memory>
#include "grace/audio/audio_backend.hpp"
namespace grace {
namespace audio {
/// OpenSL ES audio backend: the contract forwarded to the Android
/// player objects. The constructor initialises the engine and the
/// output mix; failures are reported through describe() and the
/// violation counter.
class OpenSLAudioBackend : public AudioBackend
{
public:
    OpenSLAudioBackend();
    ~OpenSLAudioBackend() override;
    OpenSLAudioBackend(const OpenSLAudioBackend&) = delete;
    OpenSLAudioBackend& operator=(const OpenSLAudioBackend&) = delete;
    OpenSLAudioBackend(OpenSLAudioBackend&&) = delete;
    OpenSLAudioBackend& operator=(OpenSLAudioBackend&&) = delete;
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
