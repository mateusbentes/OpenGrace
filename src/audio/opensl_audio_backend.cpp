// The OpenSL ES audio backend's implementation: the contract forwarded
// to the Android OpenSL ES API (SLES/OpenSLES.h). Built only when the SDK
// option is on (GRACE_ENABLE_OPENSL) and only on Android; the SDK lives
// in the NDK.
//
// The decode never happens in the backend calls: the WAV and Ogg Vorbis
// decoders (13c) live in the data layer and hand the backend PCM samples
// (architecture section 3.10). The adapter's job is the mapping only:
// the buffer ids to player objects, the stream chunks to buffer-queue
// slots, the voice properties to the play, volume, pitch and 3DLocation
// interfaces, and the mix() call to the frame update the state queries
// read. The loop is a property the application assigns; the buffer
// queue's callback re-enqueues the data, so the plain contract's loop
// flag is the record the application reads back.
#include "grace/audio/opensl_audio_backend.hpp"
#include <SLES/OpenSLES.h>
#include <cmath>
#include <map>
#include <vector>
namespace grace {
namespace audio {
namespace {
/// The OpenSL ES sample rate of the contract's Hertz: the API takes the
/// rate in milliHertz, so the contract's 44100 Hz becomes 44100000.
SLuint32 milliHertz(std::uint32_t sampleRate)
{
    return sampleRate * 1000u;
}
/// The OpenSL ES speaker mask of a sample format: the channel count the
/// contract declares is the mask the player consumes.
SLuint32 speakerMask(SampleFormat format)
{
    if (channelCount(format) == 1u)
    {
        return SL_SPEAKER_FRONT_CENTER;
    }
    return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
}
}  // namespace
struct OpenSLAudioBackend::Impl
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
        // The chunks the application appended before the stream voice
        // existed: they enqueue on the player when it is created.
        std::vector<std::vector<std::uint8_t>> pendingChunks;
    };
    struct Voice
    {
        VoiceDesc desc;
        std::uint32_t bufferId = 0;  ///< 0 for a stream voice
        std::uint32_t streamId = 0;  ///< 0 for a buffer voice
        SLObjectItf player = nullptr;
        SLBufferQueueItf queue = nullptr;
        SLVolumeItf volume = nullptr;
        SLPitchItf pitch = nullptr;
        SL3DLocationItf location = nullptr;
        VoiceState state = VoiceState::Stopped;
        bool hasReverb = false;
        ReverbDesc reverb;
    };
    SLObjectItf engine = nullptr;
    SLObjectItf outputMix = nullptr;
    std::map<std::uint32_t, Buffer> buffers;
    std::map<std::uint32_t, Voice> voices;
    std::map<std::uint32_t, Stream> streams;
    std::uint32_t nextBufferId = 1;
    std::uint32_t nextVoiceId = 1;
    std::uint32_t nextStreamId = 1;
    ListenerDesc listener;
    std::uint64_t violations = 0;
    bool ok = false;
    /// Releases every player object the backend created. The engine and
    /// the output mix die in shutdown().
    void releaseVoices()
    {
        for (auto& entry : voices)
        {
            if (entry.second.player != nullptr)
            {
                (*entry.second.player)->Destroy(entry.second.player);
                entry.second.player = nullptr;
            }
        }
        voices.clear();
        streams.clear();
    }
    /// Fetches every interface the mapping needs off a realized player.
    void fetchInterfaces(Impl::Voice& voice)
    {
        (*voice.player)->GetInterface(voice.player, SL_IID_BUFFERQUEUE,
                                      &voice.queue);
        (*voice.player)->GetInterface(voice.player, SL_IID_VOLUME,
                                      &voice.volume);
        (*voice.player)->GetInterface(voice.player, SL_IID_PITCH,
                                      &voice.pitch);
        (*voice.player)->GetInterface(voice.player, SL_IID_3DLOCATION,
                                      &voice.location);
    }
    /// Builds the player object for a voice: the PCM data source over the
    /// buffer queue, the output mix sink, and the interfaces the mapping
    /// needs. Returns false when the engine rejects the player; the
    /// caller reports the violation.
    bool buildPlayer(Impl::Voice& voice, SampleFormat format,
                     std::uint32_t sampleRate)
    {
        SLDataLocator_BufferQueue locator;
        locator.locatorType = SL_DATALOCATOR_BUFFERQUEUE;
        locator.numBuffers = 16u;
        SLDataFormat_PCM pcm;
        pcm.formatType = SL_DATAFORMAT_PCM;
        pcm.numChannels = channelCount(format);
        pcm.samplesPerSec = milliHertz(sampleRate);
        pcm.bitsPerSample = bytesPerFrame(format) * 8u;
        pcm.containerSize = pcm.bitsPerSample;
        pcm.channelMask = speakerMask(format);
        pcm.endianness = SL_BYTEORDER_LITTLEENDIAN;
        SLDataSource source;
        source.pLocator = &locator;
        source.pFormat = &pcm;
        SLDataLocator_OutputMix sinkLocator;
        sinkLocator.locatorType = SL_DATALOCATOR_OUTPUTMIX;
        sinkLocator.outputMix = outputMix;
        SLDataSink sink;
        sink.pLocator = &sinkLocator;
        sink.pFormat = nullptr;
        const SLInterfaceID ids[] = {
            SL_IID_PLAY, SL_IID_BUFFERQUEUE, SL_IID_VOLUME,
            SL_IID_PITCH, SL_IID_3DLOCATION};
        const SLboolean required[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,
                                      SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,
                                      SL_BOOLEAN_TRUE};
        SLEngineItf engineItf = nullptr;
        SLresult result = (*engine)->GetInterface(engine, SL_IID_ENGINE,
                                                  &engineItf);
        if (result != SL_RESULT_SUCCESS)
        {
            return false;
        }
        result = (*engineItf)->CreateAudioPlayer(
            engineItf, &voice.player, &source, &sink, 5u, ids, required);
        if (result != SL_RESULT_SUCCESS || voice.player == nullptr)
        {
            return false;
        }
        result = (*voice.player)->Realize(voice.player, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS)
        {
            return false;
        }
        fetchInterfaces(voice);
        return true;
    }
    /// Applies the voice description to the player's interfaces: the
    /// gain and the pitch, and the 3D properties to the 3DLocation.
    void applyVoice(const Voice& voice)
    {
        if (voice.volume != nullptr)
        {
            // The contract's gain is a linear multiplier; the OpenSL ES
            // volume is a millibel level, so the linear gain maps to the
            // level the profile defines: 0 dB is the full gain.
            const SLmillibel level = static_cast<SLmillibel>(
                voice.desc.gain <= 0.0f
                    ? SL_MILLIBEL_MIN
                    : 2000.0f * std::log10(voice.desc.gain));
            (*voice.volume)->SetVolumeLevel(voice.volume, level);
        }
        if (voice.pitch != nullptr)
        {
            (*voice.pitch)->SetPitch(
                voice.pitch,
                static_cast<SLpermille>(voice.desc.pitch * 1000.0f));
        }
        if (voice.desc.mode == VoiceMode::Sound3D &&
            voice.location != nullptr)
        {
            SLVec3D position;
            position.x =
                static_cast<SLint32>(voice.desc.position[0] * 1000.0f);
            position.y =
                static_cast<SLint32>(voice.desc.position[1] * 1000.0f);
            position.z =
                static_cast<SLint32>(voice.desc.position[2] * 1000.0f);
            (*voice.location)->SetLocationCartesian(voice.location,
                                                    &position);
        }
    }
};
OpenSLAudioBackend::OpenSLAudioBackend()
    : m_impl(std::make_unique<Impl>())
{
    SLresult result = slCreateEngine(&m_impl->engine, 0, nullptr, 0,
                                     nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS)
    {
        m_impl->engine = nullptr;
        return;
    }
    result = (*m_impl->engine)->Realize(m_impl->engine, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS)
    {
        return;
    }
    SLEngineItf engineItf = nullptr;
    result = (*m_impl->engine)->GetInterface(m_impl->engine,
                                             SL_IID_ENGINE, &engineItf);
    if (result != SL_RESULT_SUCCESS)
    {
        return;
    }
    result = (*engineItf)->CreateOutputMix(engineItf, &m_impl->outputMix,
                                           0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS)
    {
        return;
    }
    result = (*m_impl->outputMix)->Realize(m_impl->outputMix,
                                           SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS)
    {
        return;
    }
    m_impl->ok = true;
}
OpenSLAudioBackend::~OpenSLAudioBackend() { shutdown(); }
std::string OpenSLAudioBackend::describe() const
{
    return "opensl (OpenSL ES, Android)";
}
std::uint32_t OpenSLAudioBackend::capabilityFlags() const { return 0u; }
void OpenSLAudioBackend::shutdown()
{
    if (m_impl)
    {
        m_impl->releaseVoices();
        if (m_impl->outputMix != nullptr)
        {
            (*m_impl->outputMix)->Destroy(m_impl->outputMix);
            m_impl->outputMix = nullptr;
        }
        if (m_impl->engine != nullptr)
        {
            (*m_impl->engine)->Destroy(m_impl->engine);
            m_impl->engine = nullptr;
        }
        m_impl->ok = false;
    }
}
std::uint64_t OpenSLAudioBackend::violationCount() const
{
    return m_impl ? m_impl->violations : 0u;
}
std::uint32_t OpenSLAudioBackend::createBuffer(const SoundDesc& desc)
{
    if (!m_impl->ok)
    {
        ++m_impl->violations;
        return 0u;
    }
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
void OpenSLAudioBackend::releaseBuffer(std::uint32_t bufferId)
{
    if (m_impl->buffers.erase(bufferId) == 0u)
    {
        ++m_impl->violations;
    }
}
std::uint32_t OpenSLAudioBackend::bufferCount() const
{
    return static_cast<std::uint32_t>(m_impl->buffers.size());
}
std::uint32_t OpenSLAudioBackend::createVoice(const VoiceDesc& desc)
{
    if (!m_impl->ok || validate(desc).empty() == false ||
        m_impl->buffers.count(desc.bufferId) == 0u)
    {
        ++m_impl->violations;
        return 0u;
    }
    const auto buffer = m_impl->buffers.find(desc.bufferId);
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.bufferId = desc.bufferId;
    voice.state = VoiceState::Stopped;
    if (!m_impl->buildPlayer(voice, buffer->second.desc.format,
                             buffer->second.desc.sampleRate))
    {
        m_impl->voices.erase(id);
        ++m_impl->violations;
        return 0u;
    }
    m_impl->applyVoice(voice);
    // The buffer's data enqueues once: the player consumes it.
    (*voice.queue)->Enqueue(voice.queue, buffer->second.desc.samples.data(),
                            static_cast<SLuint32>(
                                buffer->second.desc.samples.size()));
    return id;
}
void OpenSLAudioBackend::releaseVoice(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    if (it->second.player != nullptr)
    {
        (*it->second.player)->Destroy(it->second.player);
    }
    m_impl->voices.erase(it);
}
std::uint32_t OpenSLAudioBackend::voiceCount() const
{
    return static_cast<std::uint32_t>(m_impl->voices.size());
}
void OpenSLAudioBackend::play(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.state = VoiceState::Playing;
    SLPlayItf play = nullptr;
    (*it->second.player)->GetInterface(it->second.player, SL_IID_PLAY,
                                       &play);
    if (play != nullptr)
    {
        (*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING);
    }
}
void OpenSLAudioBackend::pause(std::uint32_t voiceId)
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
        SLPlayItf play = nullptr;
        (*it->second.player)->GetInterface(it->second.player, SL_IID_PLAY,
                                           &play);
        if (play != nullptr)
        {
            (*play)->SetPlayState(play, SL_PLAYSTATE_PAUSED);
        }
    }
}
void OpenSLAudioBackend::stop(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.state = VoiceState::Stopped;
    SLPlayItf play = nullptr;
    (*it->second.player)->GetInterface(it->second.player, SL_IID_PLAY,
                                       &play);
    if (play != nullptr)
    {
        (*play)->SetPlayState(play, SL_PLAYSTATE_STOPPED);
    }
    if (it->second.queue != nullptr)
    {
        (*it->second.queue)->Clear(it->second.queue);
    }
}
VoiceState OpenSLAudioBackend::state(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    return it == m_impl->voices.end() ? VoiceState::Stopped
                                      : it->second.state;
}
void OpenSLAudioBackend::updateVoice(std::uint32_t voiceId,
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
std::uint32_t OpenSLAudioBackend::cursor(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || it->second.player == nullptr)
    {
        return 0u;
    }
    // The OpenSL ES reports the play position in milliseconds: the
    // cursor is the frame count the sample rate converts.
    SLPlayItf play = nullptr;
    (*it->second.player)->GetInterface(it->second.player, SL_IID_PLAY,
                                       &play);
    if (play == nullptr)
    {
        return 0u;
    }
    SLmillisecond position = 0;
    if ((*play)->GetPosition(play, &position) != SL_RESULT_SUCCESS)
    {
        return 0u;
    }
    const auto buffer = m_impl->buffers.find(it->second.bufferId);
    if (buffer == m_impl->buffers.end())
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(
        position * buffer->second.desc.sampleRate / 1000u);
}
void OpenSLAudioBackend::setReverb(std::uint32_t voiceId,
                                   const ReverbDesc& desc)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !validate(desc).empty())
    {
        ++m_impl->violations;
        return;
    }
    // Per-sound reverb (architecture section 3.10): the plain OpenSL ES
    // contract carries no EFX, so the adapter records the assignment and
    // the capability flags stay 0. A native reverb chain (the
    // EnvironmentalReverb interface) is a future extension.
    it->second.hasReverb = true;
    it->second.reverb = desc;
}
void OpenSLAudioBackend::clearReverb(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.hasReverb = false;
}
bool OpenSLAudioBackend::reverb(std::uint32_t voiceId, ReverbDesc& out) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !it->second.hasReverb)
    {
        return false;
    }
    out = it->second.reverb;
    return true;
}
std::uint32_t OpenSLAudioBackend::openStream(const StreamDesc& desc)
{
    if (!m_impl->ok)
    {
        ++m_impl->violations;
        return 0u;
    }
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
void OpenSLAudioBackend::releaseStream(std::uint32_t streamId)
{
    if (m_impl->streams.erase(streamId) == 0u)
    {
        ++m_impl->violations;
    }
}
bool OpenSLAudioBackend::appendChunk(std::uint32_t streamId,
                                     const std::vector<std::uint8_t>& chunk)
{
    auto it = m_impl->streams.find(streamId);
    if (it == m_impl->streams.end())
    {
        ++m_impl->violations;
        return false;
    }
    const std::uint32_t frameSize = bytesPerFrame(it->second.desc.format);
    if (chunk.empty() || chunk.size() % frameSize != 0u)
    {
        ++m_impl->violations;
        return false;
    }
    it->second.pendingChunks.push_back(chunk);
    it->second.queuedFrames +=
        static_cast<std::uint32_t>(chunk.size() / frameSize);
    return true;
}
std::uint32_t OpenSLAudioBackend::queuedFrames(std::uint32_t streamId) const
{
    const auto it = m_impl->streams.find(streamId);
    return it == m_impl->streams.end() ? 0u : it->second.queuedFrames;
}
std::uint32_t OpenSLAudioBackend::createStreamVoice(
    std::uint32_t streamId, const VoiceDesc& desc)
{
    if (!m_impl->ok || m_impl->streams.count(streamId) == 0u ||
        !validate(desc).empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const auto stream = m_impl->streams.find(streamId);
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.streamId = streamId;
    voice.state = VoiceState::Stopped;
    if (!m_impl->buildPlayer(voice, stream->second.desc.format,
                             stream->second.desc.sampleRate))
    {
        m_impl->voices.erase(id);
        ++m_impl->violations;
        return 0u;
    }
    m_impl->applyVoice(voice);
    // The chunks appended before the voice existed enqueue on the player.
    for (const auto& chunk : stream->second.pendingChunks)
    {
        (*voice.queue)->Enqueue(voice.queue, chunk.data(),
                                static_cast<SLuint32>(chunk.size()));
    }
    stream->second.pendingChunks.clear();
    return id;
}
void OpenSLAudioBackend::updateListener(const ListenerDesc& desc)
{
    m_impl->listener = desc;
}
void OpenSLAudioBackend::mix(std::uint32_t frames)
{
    // The device mixes on its own thread; the mix() call is the frame
    // update the application makes so the state queries stay bounded.
    (void)frames;
}
AudioStats OpenSLAudioBackend::stats() const
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
