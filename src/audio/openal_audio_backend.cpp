// The OpenAL Soft adapter's implementation: the contract forwarded to the
// device through the OpenAL Soft API. The adapter's job is the mapping
// only: the buffer ids to AL buffers, the voice ids to AL sources, the
// stream to queued AL buffers, and the mix to the device's context
// update. The decode never happens here (architecture section 3.10): the
// data layer hands PCM samples to createBuffer() and appendChunk().
#include "grace/audio/openal_audio_backend.hpp"
#include <al.h>
#include <alc.h>
#include <cmath>
#include <map>
#include <vector>
namespace grace {
namespace audio {
namespace {
/// The OpenAL format of a sample format.
ALenum alFormat(SampleFormat format)
{
    switch (format)
    {
    case SampleFormat::Mono8:
        return AL_FORMAT_MONO8;
    case SampleFormat::Mono16:
        return AL_FORMAT_MONO16;
    case SampleFormat::Stereo8:
        return AL_FORMAT_STEREO8;
    case SampleFormat::Stereo16:
        return AL_FORMAT_STEREO16;
    }
    return AL_FORMAT_MONO16;
}
}  // namespace
struct OpenALAudioBackend::Impl
{
    struct Buffer
    {
        ALuint alBuffer = 0u;
        std::uint32_t frames = 0;
    };
    struct Stream
    {
        StreamDesc desc;
        std::uint32_t queuedFrames = 0;
        // The chunks the application appended before the stream voice
        // existed: they queue on the source when the voice is created.
        std::vector<ALuint> pendingChunks;
        std::vector<ALuint> chunkBuffers;  // every chunk's AL buffer
    };
    struct Voice
    {
        VoiceDesc desc;
        ALuint alSource = 0u;
        std::uint32_t bufferId = 0;  ///< 0 for a stream voice
        std::uint32_t streamId = 0;  ///< 0 for a buffer voice
        VoiceState state = VoiceState::Stopped;
        bool hasReverb = false;
        ReverbDesc reverb;
    };
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    std::map<std::uint32_t, Buffer> buffers;
    std::map<std::uint32_t, Voice> voices;
    std::map<std::uint32_t, Stream> streams;
    std::uint32_t nextBufferId = 1;
    std::uint32_t nextVoiceId = 1;
    std::uint32_t nextStreamId = 1;
    ListenerDesc listener;
    std::uint64_t violations = 0;
    bool ok = false;
    /// Applies the voice description to the AL source: the gain, the
    /// pitch, the loop and the 3D properties.
    void applyVoice(const Voice& voice)
    {
        alSourcef(voice.alSource, AL_GAIN, voice.desc.gain);
        alSourcef(voice.alSource, AL_PITCH, voice.desc.pitch);
        alSourcei(voice.alSource, AL_LOOPING,
                  voice.desc.loop ? AL_TRUE : AL_FALSE);
        if (voice.desc.mode == VoiceMode::Sound3D)
        {
            alSourcei(voice.alSource, AL_SOURCE_RELATIVE, AL_FALSE);
            alSourcef(voice.alSource, AL_REFERENCE_DISTANCE,
                      voice.desc.referenceDistance);
            alSourcef(voice.alSource, AL_MAX_DISTANCE, voice.desc.maxDistance);
            alSource3f(voice.alSource, AL_POSITION, voice.desc.position[0],
                       voice.desc.position[1], voice.desc.position[2]);
        }
        else
        {
            alSourcei(voice.alSource, AL_SOURCE_RELATIVE, AL_TRUE);
            alSource3f(voice.alSource, AL_POSITION, 0.0f, 0.0f, 0.0f);
        }
    }
};
OpenALAudioBackend::OpenALAudioBackend() : m_impl(std::make_unique<Impl>()) {}
OpenALAudioBackend::~OpenALAudioBackend() { shutdown(); }
std::string OpenALAudioBackend::describe() const
{
    if (!m_impl->ok)
    {
        return "openal (no device: the constructor did not open one)";
    }
    const char* name = alcGetString(m_impl->device, ALC_DEVICE_SPECIFIER);
    return std::string("openal (device: ") + (name ? name : "unknown") + ")";
}
std::uint32_t OpenALAudioBackend::capabilityFlags() const { return 0u; }
void OpenALAudioBackend::shutdown()
{
    if (!m_impl)
    {
        return;
    }
    for (auto& entry : m_impl->voices)
    {
        if (m_impl->context)
        {
            alcMakeContextCurrent(m_impl->context);
            alDeleteSources(1, &entry.second.alSource);
        }
    }
    for (auto& entry : m_impl->buffers)
    {
        if (m_impl->context)
        {
            alcMakeContextCurrent(m_impl->context);
            alDeleteBuffers(1, &entry.second.alBuffer);
        }
    }
    m_impl->voices.clear();
    m_impl->buffers.clear();
    m_impl->streams.clear();
    if (m_impl->context)
    {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(m_impl->context);
        m_impl->context = nullptr;
    }
    if (m_impl->device)
    {
        alcCloseDevice(m_impl->device);
        m_impl->device = nullptr;
    }
    m_impl->ok = false;
}
std::uint64_t OpenALAudioBackend::violationCount() const
{
    return m_impl ? m_impl->violations : 0u;
}
std::uint32_t OpenALAudioBackend::createBuffer(const SoundDesc& desc)
{
    // The constructor opens the device lazily: the first call that needs
    // the context. A device that did not open reports a violation and 0.
    if (!m_impl->ok)
    {
        m_impl->device = alcOpenDevice(nullptr);
        if (!m_impl->device)
        {
            ++m_impl->violations;
            return 0u;
        }
        m_impl->context = alcCreateContext(m_impl->device, nullptr);
        if (!m_impl->context)
        {
            alcCloseDevice(m_impl->device);
            m_impl->device = nullptr;
            ++m_impl->violations;
            return 0u;
        }
        alcMakeContextCurrent(m_impl->context);
        m_impl->ok = true;
    }
    if (!validate(desc).empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextBufferId++;
    Impl::Buffer& buffer = m_impl->buffers[id];
    alGenBuffers(1, &buffer.alBuffer);
    alBufferData(buffer.alBuffer, alFormat(desc.format), desc.samples.data(),
                 static_cast<ALsizei>(desc.samples.size()),
                 static_cast<ALsizei>(desc.sampleRate));
    buffer.frames = frameCount(desc.samples, desc.format);
    return id;
}
void OpenALAudioBackend::releaseBuffer(std::uint32_t bufferId)
{
    auto it = m_impl->buffers.find(bufferId);
    if (it == m_impl->buffers.end())
    {
        ++m_impl->violations;
        return;
    }
    if (m_impl->ok)
    {
        alDeleteBuffers(1, &it->second.alBuffer);
    }
    m_impl->buffers.erase(it);
}
std::uint32_t OpenALAudioBackend::bufferCount() const
{
    return static_cast<std::uint32_t>(m_impl->buffers.size());
}
std::uint32_t OpenALAudioBackend::createVoice(const VoiceDesc& desc)
{
    if (!m_impl->ok || !validate(desc).empty() ||
        m_impl->buffers.count(desc.bufferId) == 0u)
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.bufferId = desc.bufferId;
    alGenSources(1, &voice.alSource);
    alSourcei(voice.alSource, AL_BUFFER,
              static_cast<ALint>(m_impl->buffers[desc.bufferId].alBuffer));
    m_impl->applyVoice(voice);
    return id;
}
void OpenALAudioBackend::releaseVoice(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    if (m_impl->ok)
    {
        alSourceStop(it->second.alSource);
        alSourcei(it->second.alSource, AL_BUFFER, 0);
        alDeleteSources(1, &it->second.alSource);
    }
    m_impl->voices.erase(it);
}
std::uint32_t OpenALAudioBackend::voiceCount() const
{
    return static_cast<std::uint32_t>(m_impl->voices.size());
}
void OpenALAudioBackend::play(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    alSourceRewind(it->second.alSource);
    alSourcePlay(it->second.alSource);
    it->second.state = VoiceState::Playing;
}
void OpenALAudioBackend::pause(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    alSourcePause(it->second.alSource);
    it->second.state = VoiceState::Paused;
}
void OpenALAudioBackend::stop(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    alSourceStop(it->second.alSource);
    it->second.state = VoiceState::Stopped;
}
VoiceState OpenALAudioBackend::state(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        return VoiceState::Stopped;
    }
    if (m_impl->ok)
    {
        ALint state = 0;
        alGetSourcei(it->second.alSource, AL_SOURCE_STATE, &state);
        if (state == AL_PLAYING)
        {
            return VoiceState::Playing;
        }
        if (state == AL_PAUSED)
        {
            return VoiceState::Paused;
        }
        return VoiceState::Stopped;
    }
    return it->second.state;
}
void OpenALAudioBackend::updateVoice(std::uint32_t voiceId,
                                     const VoiceDesc& desc)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !validate(desc).empty())
    {
        ++m_impl->violations;
        return;
    }
    it->second.desc = desc;
    m_impl->applyVoice(it->second);
}
std::uint32_t OpenALAudioBackend::cursor(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        return 0u;
    }
    if (!m_impl->ok)
    {
        return 0u;
    }
    ALint position = 0;
    alGetSourcei(it->second.alSource, AL_SAMPLE_OFFSET, &position);
    return position < 0 ? 0u : static_cast<std::uint32_t>(position);
}
void OpenALAudioBackend::setReverb(std::uint32_t voiceId,
                                   const ReverbDesc& desc)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !validate(desc).empty())
    {
        ++m_impl->violations;
        return;
    }
    // Per-sound reverb (architecture section 3.10): the plain OpenAL
    // contract carries no EFX, so the adapter records the assignment and
    // the capability flags stay 0. An EFX backend reports the bit and
    // forwards the zone to the slot chain.
    it->second.hasReverb = true;
    it->second.reverb = desc;
}
void OpenALAudioBackend::clearReverb(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.hasReverb = false;
}
bool OpenALAudioBackend::reverb(std::uint32_t voiceId, ReverbDesc& out) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !it->second.hasReverb)
    {
        return false;
    }
    out = it->second.reverb;
    return true;
}
std::uint32_t OpenALAudioBackend::openStream(const StreamDesc& desc)
{
    if (!m_impl->ok || !validate(desc).empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextStreamId++;
    Impl::Stream& stream = m_impl->streams[id];
    stream.desc = desc;
    return id;
}
void OpenALAudioBackend::releaseStream(std::uint32_t streamId)
{
    auto it = m_impl->streams.find(streamId);
    if (it == m_impl->streams.end())
    {
        ++m_impl->violations;
        return;
    }
    if (m_impl->ok)
    {
        for (const ALuint alBuffer : it->second.chunkBuffers)
        {
            alDeleteBuffers(1, &alBuffer);
        }
    }
    m_impl->streams.erase(it);
}
bool OpenALAudioBackend::appendChunk(std::uint32_t streamId,
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
    // The streaming path: one AL buffer per chunk. A stream without a
    // voice yet keeps the chunk pending; createStreamVoice() queues the
    // pending chunks on the source, and appendChunk() after that queues
    // the chunk on the source directly.
    ALuint alBuffer = 0u;
    alGenBuffers(1, &alBuffer);
    alBufferData(alBuffer, alFormat(it->second.desc.format), chunk.data(),
                 static_cast<ALsizei>(chunk.size()),
                 static_cast<ALsizei>(it->second.desc.sampleRate));
    it->second.chunkBuffers.push_back(alBuffer);
    it->second.queuedFrames +=
        static_cast<std::uint32_t>(chunk.size() / frameSize);
    // A voice of this stream queues the chunk on the source directly; a
    // stream without a voice yet keeps the chunk pending, and
    // createStreamVoice() queues the pending chunks on the source.
    bool hasVoice = false;
    for (auto& entry : m_impl->voices)
    {
        if (entry.second.streamId == streamId)
        {
            alSourceQueueBuffers(entry.second.alSource, 1, &alBuffer);
            hasVoice = true;
        }
    }
    if (!hasVoice)
    {
        it->second.pendingChunks.push_back(alBuffer);
    }
    return true;
}
std::uint32_t OpenALAudioBackend::queuedFrames(std::uint32_t streamId) const
{
    const auto it = m_impl->streams.find(streamId);
    return it == m_impl->streams.end() ? 0u : it->second.queuedFrames;
}
std::uint32_t OpenALAudioBackend::createStreamVoice(std::uint32_t streamId,
                                                    const VoiceDesc& desc)
{
    if (!m_impl->ok || m_impl->streams.count(streamId) == 0u ||
        !validate(desc).empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.streamId = streamId;
    voice.state = VoiceState::Stopped;
    alGenSources(1, &voice.alSource);
    m_impl->applyVoice(voice);
    // The chunks appended before the voice existed queue on the source.
    auto stream = m_impl->streams.find(streamId);
    if (!stream->second.pendingChunks.empty())
    {
        alSourceQueueBuffers(voice.alSource,
                             static_cast<ALsizei>(
                                 stream->second.pendingChunks.size()),
                             stream->second.pendingChunks.data());
        stream->second.pendingChunks.clear();
    }
    return id;
}
void OpenALAudioBackend::updateListener(const ListenerDesc& desc)
{
    m_impl->listener = desc;
    if (!m_impl->ok)
    {
        return;
    }
    alListenerf(AL_GAIN, desc.gain);
    alListener3f(AL_POSITION, desc.position[0], desc.position[1],
                 desc.position[2]);
    ALfloat orientation[6] = {desc.forward[0], desc.forward[1],
                              desc.forward[2], desc.up[0], desc.up[1],
                              desc.up[2]};
    alListenerfv(AL_ORIENTATION, orientation);
}
void OpenALAudioBackend::mix(std::uint32_t frames)
{
    // The device mixes on its own thread; the mix() call is the frame
    // update the application makes so the state queries stay bounded.
    // The OpenAL adapter has nothing to advance: the device's context
    // processes the queued sources.
    (void)frames;
}
AudioStats OpenALAudioBackend::stats() const
{
    AudioStats stats;
    stats.bufferCount = static_cast<std::uint32_t>(m_impl->buffers.size());
    stats.voiceCount = static_cast<std::uint32_t>(m_impl->voices.size());
    stats.streamCount = static_cast<std::uint32_t>(m_impl->streams.size());
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
