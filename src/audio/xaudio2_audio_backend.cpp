// The XAudio2 audio backend's implementation: the contract forwarded to
// the XAudio2.8 mixer (xaudio2.h). Built only when the SDK option is on
// (GRACE_ENABLE_XAUDIO2) and only on Windows; the SDK lives outside the
// repository.
//
// The decode never happens in the backend calls: the WAV and Ogg Vorbis
// decoders (13c) live in the data layer and hand the backend PCM samples
// (architecture section 3.10). The adapter's job is the mapping only:
// the buffer ids to submitted source buffers, the voice ids to source
// voices, the stream chunks to submitted buffers, the voice properties
// to the source voice's state, and the mix() call to the engine's
// processing pass. The reverb assignment is recorded like the OpenAL
// adapter records it: the plain XAudio2 contract's per-sound reverb is
// the application's assignment, and a native reverb chain (XAPO) is a
// future extension.
#include "grace/audio/xaudio2_audio_backend.hpp"
#include <windows.h>
#include <xaudio2.h>
#include <cmath>
#include <map>
#include <vector>
namespace grace {
namespace audio {
namespace {
/// The XAudio2 format of a sample format: one WAVEFORMATEX per shape.
/// The XAudio2 source voice consumes the sample data exactly as the
/// contract declares it: the byte count is the frame count times the
/// frame size.
WAVEFORMATEX waveFormat(SampleFormat sampleFormat, std::uint32_t sampleRate)
{
    WAVEFORMATEX format;
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(channelCount(sampleFormat));
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = static_cast<WORD>(
        bytesPerFrame(sampleFormat) * 8u);
    format.nBlockAlign = static_cast<WORD>(bytesPerFrame(sampleFormat));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;
    return format;
}
}  // namespace
struct XAudio2AudioBackend::Impl
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
        // existed: they submit to the source when the voice is created.
        std::vector<std::vector<std::uint8_t>> pendingChunks;
    };
    struct Voice
    {
        VoiceDesc desc;
        std::uint32_t bufferId = 0;  ///< 0 for a stream voice
        std::uint32_t streamId = 0;  ///< 0 for a buffer voice
        IXAudio2SourceVoice* source = nullptr;
        VoiceState state = VoiceState::Stopped;
        bool hasReverb = false;
        ReverbDesc reverb;
    };
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* mastering = nullptr;
    std::map<std::uint32_t, Buffer> buffers;
    std::map<std::uint32_t, Voice> voices;
    std::map<std::uint32_t, Stream> streams;
    std::uint32_t nextBufferId = 1;
    std::uint32_t nextVoiceId = 1;
    std::uint32_t nextStreamId = 1;
    ListenerDesc listener;
    std::uint64_t violations = 0;
    bool ok = false;
    /// Releases every source voice the backend created. The mastering
    /// voice dies with the engine.
    void releaseVoices()
    {
        for (auto& entry : voices)
        {
            if (entry.second.source != nullptr)
            {
                entry.second.source->DestroyVoice();
                entry.second.source = nullptr;
            }
        }
        voices.clear();
        streams.clear();
    }
    /// Applies the voice description to the source voice: the gain and
    /// the pitch. The 3D properties ride the buffer's data the device
    /// pans, so the plain contract's 2D gain applies to the voice.
    void applyVoice(const Voice& voice)
    {
        if (voice.source == nullptr)
        {
            return;
        }
        voice.source->SetVolume(voice.desc.gain);
        voice.source->SetFrequencyRatio(voice.desc.pitch);
    }
};
XAudio2AudioBackend::XAudio2AudioBackend()
    : m_impl(std::make_unique<Impl>())
{
    HRESULT result = XAudio2Create(&m_impl->engine, 0,
                                   XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(result))
    {
        m_impl->engine = nullptr;
        return;
    }
    result = m_impl->engine->CreateMasteringVoice(&m_impl->mastering);
    if (FAILED(result))
    {
        m_impl->engine->Release();
        m_impl->engine = nullptr;
        return;
    }
    m_impl->ok = true;
}
XAudio2AudioBackend::~XAudio2AudioBackend() { shutdown(); }
std::string XAudio2AudioBackend::describe() const
{
    return "xaudio2 (XAudio2.8, Windows)";
}
std::uint32_t XAudio2AudioBackend::capabilityFlags() const { return 0u; }
void XAudio2AudioBackend::shutdown()
{
    if (m_impl)
    {
        m_impl->releaseVoices();
        if (m_impl->mastering != nullptr)
        {
            m_impl->mastering->DestroyVoice();
            m_impl->mastering = nullptr;
        }
        if (m_impl->engine != nullptr)
        {
            m_impl->engine->Release();
            m_impl->engine = nullptr;
        }
        m_impl->ok = false;
    }
}
std::uint64_t XAudio2AudioBackend::violationCount() const
{
    return m_impl ? m_impl->violations : 0u;
}
std::uint32_t XAudio2AudioBackend::createBuffer(const SoundDesc& desc)
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
void XAudio2AudioBackend::releaseBuffer(std::uint32_t bufferId)
{
    if (m_impl->buffers.erase(bufferId) == 0u)
    {
        ++m_impl->violations;
    }
}
std::uint32_t XAudio2AudioBackend::bufferCount() const
{
    return static_cast<std::uint32_t>(m_impl->buffers.size());
}
std::uint32_t XAudio2AudioBackend::createVoice(const VoiceDesc& desc)
{
    if (!m_impl->ok || validate(desc).empty() == false ||
        m_impl->buffers.count(desc.bufferId) == 0u)
    {
        ++m_impl->violations;
        return 0u;
    }
    const auto buffer = m_impl->buffers.find(desc.bufferId);
    const WAVEFORMATEX format = waveFormat(buffer->second.desc.format,
                                           buffer->second.desc.sampleRate);
    IXAudio2SourceVoice* source = nullptr;
    HRESULT result = m_impl->engine->CreateSourceVoice(&source, &format);
    if (FAILED(result))
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.bufferId = desc.bufferId;
    voice.state = VoiceState::Stopped;
    voice.source = source;
    m_impl->applyVoice(voice);
    XAUDIO2_BUFFER audio;
    audio.Flags = 0;
    audio.AudioBytes =
        static_cast<UINT32>(buffer->second.desc.samples.size());
    audio.pAudioData = buffer->second.desc.samples.data();
    audio.PlayBegin = 0;
    audio.PlayLength = 0;  // the whole buffer
    if (desc.loop)
    {
        audio.LoopBegin = 0;
        audio.LoopLength = 0;
        audio.LoopCount = XAUDIO2_LOOP_INFINITE;
    }
    else
    {
        audio.LoopBegin = XAUDIO2_NO_LOOP_REGION;
        audio.LoopLength = 0;
        audio.LoopCount = 0;
    }
    audio.pContext = nullptr;
    source->SubmitSourceBuffer(&audio);
    return id;
}
void XAudio2AudioBackend::releaseVoice(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    if (it->second.source != nullptr)
    {
        it->second.source->DestroyVoice();
    }
    m_impl->voices.erase(it);
}
std::uint32_t XAudio2AudioBackend::voiceCount() const
{
    return static_cast<std::uint32_t>(m_impl->voices.size());
}
void XAudio2AudioBackend::play(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.state = VoiceState::Playing;
    it->second.source->Start(0, XAUDIO2_COMMIT_NOW);
}
void XAudio2AudioBackend::pause(std::uint32_t voiceId)
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
        it->second.source->Stop(0, XAUDIO2_COMMIT_NOW);
    }
}
void XAudio2AudioBackend::stop(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.state = VoiceState::Stopped;
    it->second.source->Stop(0, XAUDIO2_COMMIT_NOW);
    it->second.source->FlushSourceBuffers();
}
VoiceState XAudio2AudioBackend::state(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    return it == m_impl->voices.end() ? VoiceState::Stopped
                                      : it->second.state;
}
void XAudio2AudioBackend::updateVoice(std::uint32_t voiceId,
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
std::uint32_t XAudio2AudioBackend::cursor(std::uint32_t voiceId) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || it->second.source == nullptr)
    {
        return 0u;
    }
    XAUDIO2_VOICE_STATE state;
    it->second.source->GetState(&state);
    // The XAudio2's SamplesPlayed is the frame count the device has
    // played since the voice started: the cursor is the count the
    // application can read.
    return static_cast<std::uint32_t>(state.SamplesPlayed);
}
void XAudio2AudioBackend::setReverb(std::uint32_t voiceId,
                                    const ReverbDesc& desc)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !validate(desc).empty())
    {
        ++m_impl->violations;
        return;
    }
    // Per-sound reverb (architecture section 3.10): the plain XAudio2
    // contract carries no EFX, so the adapter records the assignment and
    // the capability flags stay 0. A native reverb chain (XAPO) is a
    // future extension.
    it->second.hasReverb = true;
    it->second.reverb = desc;
}
void XAudio2AudioBackend::clearReverb(std::uint32_t voiceId)
{
    auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end())
    {
        ++m_impl->violations;
        return;
    }
    it->second.hasReverb = false;
}
bool XAudio2AudioBackend::reverb(std::uint32_t voiceId, ReverbDesc& out) const
{
    const auto it = m_impl->voices.find(voiceId);
    if (it == m_impl->voices.end() || !it->second.hasReverb)
    {
        return false;
    }
    out = it->second.reverb;
    return true;
}
std::uint32_t XAudio2AudioBackend::openStream(const StreamDesc& desc)
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
void XAudio2AudioBackend::releaseStream(std::uint32_t streamId)
{
    if (m_impl->streams.erase(streamId) == 0u)
    {
        ++m_impl->violations;
    }
}
bool XAudio2AudioBackend::appendChunk(std::uint32_t streamId,
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
std::uint32_t XAudio2AudioBackend::queuedFrames(std::uint32_t streamId) const
{
    const auto it = m_impl->streams.find(streamId);
    return it == m_impl->streams.end() ? 0u : it->second.queuedFrames;
}
std::uint32_t XAudio2AudioBackend::createStreamVoice(
    std::uint32_t streamId, const VoiceDesc& desc)
{
    if (!m_impl->ok || m_impl->streams.count(streamId) == 0u ||
        !validate(desc).empty())
    {
        ++m_impl->violations;
        return 0u;
    }
    const auto stream = m_impl->streams.find(streamId);
    const WAVEFORMATEX format = waveFormat(stream->second.desc.format,
                                           stream->second.desc.sampleRate);
    IXAudio2SourceVoice* source = nullptr;
    HRESULT result = m_impl->engine->CreateSourceVoice(&source, &format);
    if (FAILED(result))
    {
        ++m_impl->violations;
        return 0u;
    }
    const std::uint32_t id = m_impl->nextVoiceId++;
    Impl::Voice& voice = m_impl->voices[id];
    voice.desc = desc;
    voice.streamId = streamId;
    voice.state = VoiceState::Stopped;
    voice.source = source;
    m_impl->applyVoice(voice);
    // The chunks appended before the voice existed submit to the source.
    for (const auto& chunk : stream->second.pendingChunks)
    {
        XAUDIO2_BUFFER audio;
        audio.Flags = 0;
        audio.AudioBytes = static_cast<UINT32>(chunk.size());
        audio.pAudioData = chunk.data();
        audio.PlayBegin = 0;
        audio.PlayLength = 0;
        audio.LoopBegin = XAUDIO2_NO_LOOP_REGION;
        audio.LoopLength = 0;
        audio.LoopCount = 0;
        audio.pContext = nullptr;
        source->SubmitSourceBuffer(&audio);
    }
    stream->second.pendingChunks.clear();
    return id;
}
void XAudio2AudioBackend::updateListener(const ListenerDesc& desc)
{
    m_impl->listener = desc;
}
void XAudio2AudioBackend::mix(std::uint32_t frames)
{
    // The device mixes on its own thread; the mix() call is the frame
    // update the application makes so the state queries stay bounded.
    // The XAudio2 adapter has nothing to advance: the device's engine
    // processes the submitted sources.
    (void)frames;
}
AudioStats XAudio2AudioBackend::stats() const
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
