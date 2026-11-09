// The null audio backend's implementation: the contract with a
// deterministic mixer. No real device is touched: the mix() call advances
// the voices' cursors by the pitch and accumulates the mixed sample
// count, so every run is reproducible bit-for-bit. The 3D voices
// attenuate by the inverse-distance model the contract declares.
#include "grace/audio/null_audio_backend.hpp"
#include <cmath>
#include <map>
#include <string>
#include <vector>
namespace grace {
namespace audio {
namespace {
/// The distance attenuation of a 3D voice: 1.0 at the reference distance,
/// 0.0 at the max distance, linear in between, clamped outside.
float attenuation(const VoiceDesc& voice, const ListenerDesc& listener)
{
    const float dx = voice.position[0] - listener.position[0];
    const float dy = voice.position[1] - listener.position[1];
    const float dz = voice.position[2] - listener.position[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float span = voice.maxDistance - voice.referenceDistance;
    if (span <= 0.0f)
    {
        return 1.0f;
    }
    const float t = (distance - voice.referenceDistance) / span;
    if (t <= 0.0f)
    {
        return 1.0f;
    }
    if (t >= 1.0f)
    {
        return 0.0f;
    }
    return 1.0f - t;
}
}  // namespace
struct NullAudioBackend::Impl
{
    struct Buffer
    {
        SoundDesc desc;
        std::uint32_t frames = 0;
    };
    struct Stream
    {
        StreamDesc desc;
        std::uint32_t queuedFrames = 0;
        std::uint32_t chunkIndex = 0;
        std::vector<std::vector<std::uint8_t>> chunks;
    };
    struct Voice
    {
        VoiceDesc desc;
        std::uint32_t bufferId = 0;  ///< 0 for a stream voice
        std::uint32_t streamId = 0;  ///< 0 for a buffer voice
        VoiceState state = VoiceState::Stopped;
        std::uint32_t cursor = 0;    ///< frames into the buffer
        bool hasReverb = false;
        ReverbDesc reverb;
    };
    std::map<std::uint32_t, Buffer> buffers;
    std::map<std::uint32_t, Voice> voices;
    std::map<std::uint32_t, Stream> streams;
    std::uint32_t nextBufferId = 1;
    std::uint32_t nextVoiceId = 1;
    std::uint32_t nextStreamId = 1;
    ListenerDesc listener;
    std::uint64_t violations = 0;
    std::uint64_t samplesMixed = 0;
    bool shutdown = false;
    /// The frames available to a voice: the buffer's frames for a buffer
    /// voice, the queued frames for a stream voice.
    std::uint32_t availableFrames(const Voice& voice) const
    {
        if (voice.bufferId != 0u)
        {
            const auto it = buffers.find(voice.bufferId);
            return it == buffers.end() ? 0u : it->second.frames;
        }
        const auto it = streams.find(voice.streamId);
        return it == streams.end() ? 0u : it->second.queuedFrames;
    }
    /// Advances one voice's cursor by the mixed frames and applies the
    /// loop rule. A stream voice that runs out of the queued chunks stops
    /// (the application re-appends chunks to keep it going).
    void advance(Voice& voice, std::uint32_t frames, float pitch)
    {
        // A stream voice consumes the stream's shared queue: the pitch
        // scales the frames it consumes, and when the queue empties the
        // voice stops (the application re-appends chunks to keep going).
        if (voice.streamId != 0u)
        {
            auto stream = streams.find(voice.streamId);
            if (stream == streams.end())
            {
                voice.state = VoiceState::Stopped;
                return;
            }
            const double wanted =
                static_cast<double>(frames) * static_cast<double>(pitch);
            const double consumed = std::min<double>(
                wanted, static_cast<double>(stream->second.queuedFrames));
            stream->second.queuedFrames -=
                static_cast<std::uint32_t>(consumed);
            // A queue that emptied during this mix ends the voice: the
            // application re-appends chunks and plays again to keep it
            // going.
            if (stream->second.queuedFrames == 0u)
            {
                voice.cursor = 0u;
                voice.state = VoiceState::Stopped;
            }
            if (consumed < wanted)
            {
                voice.cursor = 0u;
                voice.state = VoiceState::Stopped;
            }
            return;
        }
        const std::uint32_t total = availableFrames(voice);
        if (total == 0u)
        {
            voice.state = VoiceState::Stopped;
            return;
        }
        // The pitch is a rate multiplier: a pitch of 2.0 consumes twice
        // the frames per mixed frame.
        const double advanced = static_cast<double>(voice.cursor) +
                                static_cast<double>(frames) * pitch;
        if (advanced >= static_cast<double>(total))
        {
            if (voice.desc.loop && voice.bufferId != 0u)
            {
                // A looping buffer voice wraps: the cursor restarts.
                const double wrapped = advanced -
                                       std::floor(advanced / total) * total;
                voice.cursor = static_cast<std::uint32_t>(wrapped);
            }
            else
            {
                voice.cursor = 0u;
                voice.state = VoiceState::Stopped;
            }
        }
        else
        {
            voice.cursor = static_cast<std::uint32_t>(advanced);
        }
    }
};
NullAudioBackend::NullAudioBackend() : m_impl(std::make_unique<Impl>()) {}
NullAudioBackend::~NullAudioBackend() { shutdown(); }
std::string NullAudioBackend::describe() const
{
    return "null (headless, deterministic mixer, no device)";
}
std::uint32_t NullAudioBackend::capabilityFlags() const { return 0u; }
void NullAudioBackend::shutdown()
{
    if (m_impl)
    {
        m_impl->buffers.clear();
        m_impl->voices.clear();
        m_impl->streams.clear();
        m_impl->shutdown = true;
    }
}
std::uint64_t NullAudioBackend::violationCount() const
{
    return m_impl ? m_impl->violations : 0u;
}
std::uint32_t NullAudioBackend::createBuffer(const SoundDesc& desc)
{
    const std::string reason = validate(desc);
    if (!reason.empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextBufferId++;
    Impl::Buffer& buffer = m_impl->buffers[id];
    buffer.desc = desc;
    buffer.frames = frameCount(desc.samples, desc.format);
    return id;
}
void NullAudioBackend::releaseBuffer(std::uint32_t bufferId)
{
    if (m_impl->buffers.erase(bufferId) == 0u)
    {
        ++m_impl->violations;
    }
}
std::uint32_t NullAudioBackend::bufferCount() const
{
    return static_cast<std::uint32_t>(m_impl->buffers.size());
}
std::uint32_t NullAudioBackend::createVoice(const VoiceDesc& desc)
{
    if (validate(desc).empty() == false ||
        m_impl->buffers.count(desc.bufferId) == 0u)
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.bufferId = desc.bufferId;
    voice.state = VoiceState::Stopped;
    return id;
}
void NullAudioBackend::releaseVoice(std::uint32_t voiceId)
{
    if (m_impl->voices.erase(voiceId) == 0u)
    {
        ++m_impl->violations;
    }
}
std::uint32_t NullAudioBackend::voiceCount() const
{
    return static_cast<std::uint32_t>(m_impl->voices.size());
}
void NullAudioBackend::play(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.cursor = 0u;
    it->second.state = VoiceState::Playing;
}
void NullAudioBackend::pause(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    if (it->second.state == VoiceState::Playing)
    {
        it->second.state = VoiceState::Paused;
    }
}
void NullAudioBackend::stop(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.cursor = 0u;
    it->second.state = VoiceState::Stopped;
}
VoiceState NullAudioBackend::state(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    return it == m_impl->voices.end() ? VoiceState::Stopped : it->second.state;
}
void NullAudioBackend::updateVoice(std::uint32_t voiceId,
                                   const VoiceDesc& desc)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !validate(desc).empty())
    {
        ++m_impl->violations;
        return;
    }
    it->second.desc = desc;
}
std::uint32_t NullAudioBackend::cursor(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    return it == m_impl->voices.end() ? 0u : it->second.cursor;
}
void NullAudioBackend::setReverb(std::uint32_t voiceId, const ReverbDesc& desc)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !validate(desc).empty())
    {
        ++m_impl->violations;
        return;
    }
    it->second.hasReverb = true;
    it->second.reverb = desc;
}
void NullAudioBackend::clearReverb(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.hasReverb = false;
}
bool NullAudioBackend::reverb(std::uint32_t voiceId, ReverbDesc& out) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !it->second.hasReverb)
    {
        return false;
    }
    out = it->second.reverb;
    return true;
}
std::uint32_t NullAudioBackend::openStream(const StreamDesc& desc)
{
    if (!validate(desc).empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextStreamId++;
    Impl::Stream& stream = m_impl->streams[id];
    stream.desc = desc;
    return id;
}
void NullAudioBackend::releaseStream(std::uint32_t streamId)
{
    if (m_impl->streams.erase(streamId) == 0u)
    {
        ++m_impl->violations;
    }
}
bool NullAudioBackend::appendChunk(std::uint32_t streamId,
                                   const std::vector<std::uint8_t>& chunk)
{
    auto it = m_impl->streams.find(streamId);
    if (it == m_impl->streams.end())
    {
        ++m_impl->violations;
        return false;
    }
    const std::uint32_t frameSize = bytesPerFrame(it->second.desc.format);
    if (chunk.empty() || frameSize == 0u || chunk.size() % frameSize != 0u)
    {
        ++m_impl->violations;
        return false;
    }
    it->second.chunks.push_back(chunk);
    it->second.queuedFrames +=
        static_cast<std::uint32_t>(chunk.size() / frameSize);
    return true;
}
std::uint32_t NullAudioBackend::queuedFrames(std::uint32_t streamId) const
{
    const auto it = m_impl->streams.find(streamId);
    return it == m_impl->streams.end() ? 0u : it->second.queuedFrames;
}
std::uint32_t NullAudioBackend::createStreamVoice(std::uint32_t streamId,
                                                  const VoiceDesc& desc)
{
    if (m_impl->streams.count(streamId) == 0u || !validate(desc).empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.streamId = streamId;
    voice.state = VoiceState::Stopped;
    return id;
}
void NullAudioBackend::updateListener(const ListenerDesc& desc)
{
    m_impl->listener = desc;
}
void NullAudioBackend::mix(std::uint32_t frames)
{
    // The deterministic mix: every audible voice advances by the mixed
    // frames scaled by its pitch, and the mixed sample count grows by the
    // frames the listener hears. The 3D voices past the max distance
    // contribute nothing, but they still advance (the cursor moves).
    for (auto& entry : m_impl->voices)
    {
        Impl::Voice& voice = entry.second;
        if (voice.state != VoiceState::Playing)
        {
            continue;
        }
        float audible = voice.desc.gain;
        if (voice.desc.mode == VoiceMode::Sound3D)
        {
            audible *= attenuation(voice.desc, m_impl->listener);
        }
        if (voice.hasReverb)
        {
            audible += voice.reverb.wetGain * (1.0f - voice.reverb.damping);
        }
        if (audible > 0.0f)
        {
            m_impl->samplesMixed += frames;
        }
        m_impl->advance(voice, frames, voice.desc.pitch);
    }
}
AudioStats NullAudioBackend::stats() const
{
    AudioStats stats;
    stats.bufferCount = static_cast<std::uint32_t>(m_impl->buffers.size());
    stats.voiceCount = static_cast<std::uint32_t>(m_impl->voices.size());
    stats.streamCount = static_cast<std::uint32_t>(m_impl->streams.size());
    stats.samplesMixed = m_impl->samplesMixed;
    for (const auto& entry : m_impl->voices)
    {
        if (entry.second.state == VoiceState::Playing)
        {
            ++stats.playingCount;
        }
    }
    return stats;
}
}  // namespace audio
}  // namespace grace
