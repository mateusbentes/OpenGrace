// The audio adapter suite (13c): the OpenAL Soft backend and the decode
// layer. The decode never happens in the backend calls: this suite feeds
// the backend PCM samples and checks the mapping. The suite runs against
// the default device, which the sandbox provides through the null sink.
#include "contract_checks.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "grace/audio/audio_decode.hpp"
#include "grace/audio/openal_audio_backend.hpp"
namespace {
using grace::audio::Decoded;
using grace::audio::OpenALAudioBackend;
using grace::audio::ReverbDesc;
using grace::audio::SampleFormat;
using grace::audio::SoundDesc;
using grace::audio::StreamDesc;
using grace::audio::VoiceDesc;
using grace::audio::VoiceMode;
using grace::audio::VoiceState;
/// A WAV file: 16-bit mono, 44100 Hz, the frame count the caller asks.
std::vector<std::uint8_t> wavBytes(std::uint32_t frames)
{
    const std::uint32_t dataBytes = frames * 2u;
    const std::uint32_t riffSize = 36u + dataBytes;
    std::vector<std::uint8_t> bytes;
    const auto push = [&bytes](std::uint32_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    };
    const auto push16 = [&bytes](std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    };
    const auto pushTag = [&bytes](const char* tag) {
        bytes.push_back(static_cast<std::uint8_t>(tag[0]));
        bytes.push_back(static_cast<std::uint8_t>(tag[1]));
        bytes.push_back(static_cast<std::uint8_t>(tag[2]));
        bytes.push_back(static_cast<std::uint8_t>(tag[3]));
    };
    pushTag("RIFF");
    push(riffSize);
    pushTag("WAVE");
    pushTag("fmt ");
    push(16u);
    push16(1u);  // PCM
    push16(1u);  // mono
    push(44100u);
    push(44100u * 2u);  // byte rate
    push16(2u);         // block align
    push16(16u);        // 16 bits
    pushTag("data");
    push(dataBytes);
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        // A small rising ramp so the samples are not all identical.
        const std::uint16_t sample = static_cast<std::uint16_t>(frame * 2u);
        push16(sample);
    }
    return bytes;
}
void testWavDecode(OpenALAudioBackend&)
{
    const std::vector<std::uint8_t> bytes = wavBytes(64u);
    const Decoded decoded = grace::audio::decodeWav(bytes);
    GRACE_CHECK(decoded.ok);  // "the WAV decodes
    // "the WAV is mono 16-bit
    GRACE_CHECK(decoded.format == SampleFormat::Mono16);
    // "the WAV's sample rate is 44100
    GRACE_CHECK(decoded.sampleRate == 44100u);
    // "the WAV has 64 frames
    GRACE_CHECK(grace::audio::frameCount(decoded.samples, decoded.format) ==
                64u);
    // "the decoded payload passes the backend's validation
    GRACE_CHECK(grace::audio::validate(decoded).empty());
}
void testOpenALBackend(OpenALAudioBackend& backend)
{
    // "describe names openal
    GRACE_CHECK(backend.describe().find("openal") == 0u);
    GRACE_CHECK(backend.violationCount() == 0u);  // "no violations yet
    // The buffer: the decoded WAV payload.
    SoundDesc sound;
    sound.samples = grace::audio::decodeWav(wavBytes(44100u)).samples;
    sound.format = SampleFormat::Mono16;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    GRACE_CHECK(bufferId != 0u);  // "the buffer uploads
    GRACE_CHECK(backend.bufferCount() == 1u);  // "the buffer count is one
    // The voice: one playback instance.
    VoiceDesc desc;
    desc.bufferId = bufferId;
    desc.mode = VoiceMode::Sound2D;
    desc.gain = 0.5f;
    const std::uint32_t voiceId = backend.createVoice(desc);
    GRACE_CHECK(voiceId != 0u);  // "the voice is created
    // "the voice is stopped
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    backend.play(voiceId);
    backend.mix(1000u);
    // "the voice plays on the device
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Playing);
    backend.pause(voiceId);
    // "the voice pauses
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Paused);
    backend.stop(voiceId);
    // "the voice stops
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    backend.releaseVoice(voiceId);
    backend.releaseBuffer(bufferId);
    GRACE_CHECK(backend.violationCount() == 0u);  // "the lifecycle is clean
}
void testOpenALStreaming(OpenALAudioBackend& backend)
{
    StreamDesc streamDesc;
    streamDesc.format = SampleFormat::Mono16;
    streamDesc.sampleRate = 44100u;
    const std::uint32_t streamId = backend.openStream(streamDesc);
    GRACE_CHECK(streamId != 0u);  // "the stream opens
    // One second of audio: the queue has to outlive the mix the test
    // issues, or the null driver would report the voice as stopped.
    const std::vector<std::uint8_t> chunk(2u * 44100u, 0u);
    // "the chunk is appended
    GRACE_CHECK(backend.appendChunk(streamId, chunk));
    // The queue holds one second of audio.
    GRACE_CHECK(backend.queuedFrames(streamId) == 44100u);
    VoiceDesc desc;
    desc.bufferId = 0u;
    desc.mode = VoiceMode::Sound2D;
    const std::uint32_t voiceId = backend.createStreamVoice(streamId, desc);
    GRACE_CHECK(voiceId != 0u);  // "the stream voice is created
    backend.play(voiceId);
    backend.mix(1000u);
    // "the stream voice plays from the queue
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Playing);
    backend.releaseVoice(voiceId);
    backend.releaseStream(streamId);
    // "the streaming lifecycle is clean
    GRACE_CHECK(backend.violationCount() == 0u);
}
void testOpenALListener(OpenALAudioBackend& backend)
{
    grace::audio::ListenerDesc listener;
    listener.position[0] = 1.0f;
    listener.forward[0] = 0.0f;
    listener.forward[2] = -1.0f;
    listener.gain = 0.8f;
    backend.updateListener(listener);
    // "the listener update is clean
    GRACE_CHECK(backend.violationCount() == 0u);
}
void testOpenALReverb(OpenALAudioBackend& backend)
{
    SoundDesc sound;
    sound.samples.assign(44100u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    const std::uint32_t voiceId = backend.createVoice(desc);
    ReverbDesc reverb;
    reverb.decay = 1.0f;
    reverb.wetGain = 0.2f;
    backend.setReverb(voiceId, reverb);
    ReverbDesc out;
    GRACE_CHECK(backend.reverb(voiceId, out));  // "the reverb is assigned
    GRACE_CHECK(out.wetGain == 0.2f);  // "the wet gain round-trips
    backend.clearReverb(voiceId);
    GRACE_CHECK(!backend.reverb(voiceId, out));  // "the reverb is cleared
    backend.releaseVoice(voiceId);
    backend.releaseBuffer(bufferId);
}
void testOpenALForeignHandles(OpenALAudioBackend& backend)
{
    // "a foreign voice state is stopped
    GRACE_CHECK(backend.state(999u) == VoiceState::Stopped);
    backend.play(999u);
    // "the foreign handle was a violation
    GRACE_CHECK(backend.violationCount() == 1u);
    GRACE_CHECK(backend.stats().voiceCount == 0u);  // "no voices were created
}
void testVorbisDecode()
{
    // A non-Ogg payload: the decode rejects it.
    const std::vector<std::uint8_t> notOgg = {'N', 'O', 'P', 'E'};
    const Decoded failed = grace::audio::decodeVorbis(notOgg);
    GRACE_CHECK(!failed.ok);  // "a non-Ogg payload is rejected
    // "the reason is the format
    GRACE_CHECK(failed.reason == "not an Ogg container");
    // An Ogg container that is not Vorbis: the decode rejects it.
    const std::vector<std::uint8_t> oggButNotVorbis = {
        'O', 'g', 'g', 'S', 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x7Eu};
    const Decoded failed2 = grace::audio::decodeVorbis(oggButNotVorbis);
    GRACE_CHECK(!failed2.ok);  // "a non-Vorbis Ogg payload is rejected
    // "the reason is the Vorbis stream
    GRACE_CHECK(failed2.reason == "not an Ogg Vorbis stream");
}

}  // namespace
int main()
{
    OpenALAudioBackend backend;
    // A device that does not open (no audio hardware, no ALSA) is an
    // environment limit, not a failure: the suite skips with code 77, the
    // same convention the OpenGL suites follow.
    SoundDesc probe;
    probe.samples.assign(4u, 0u);
    probe.format = SampleFormat::Mono8;
    probe.sampleRate = 44100u;
    const std::uint32_t probeId = backend.createBuffer(probe);
    if (probeId == 0u)
    {
        std::fprintf(stderr, "SKIP: no audio device opened\n");
        return 77;
    }
    backend.releaseBuffer(probeId);
    testWavDecode(backend);
    testOpenALBackend(backend);
    testOpenALStreaming(backend);
    testOpenALListener(backend);
    testOpenALReverb(backend);
    testOpenALForeignHandles(backend);
    testVorbisDecode();
    return grace::test::summary("audio_adapters");
}
