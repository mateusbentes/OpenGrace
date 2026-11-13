// The XAudio2 backend's suite (22a): the mapping checks the contract
// records. The suite runs against the default device, which the sandbox
// provides through the Wine prefix; a device that does not open is an
// environment limit, not a failure: the suite skips with code 77, the
// same convention the OpenAL suite follows.
#include "contract_checks.hpp"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "grace/audio/audio_decode.hpp"
#include "grace/audio/xaudio2_audio_backend.hpp"
namespace {
using grace::audio::Decoded;
using grace::audio::ReverbDesc;
using grace::audio::SampleFormat;
using grace::audio::SoundDesc;
using grace::audio::StreamDesc;
using grace::audio::VoiceDesc;
using grace::audio::VoiceMode;
using grace::audio::VoiceState;
using grace::audio::XAudio2AudioBackend;
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
void testWavDecode()
{
    const std::vector<std::uint8_t> bytes = wavBytes(64u);
    const Decoded decoded = grace::audio::decodeWav(bytes);
    GRACE_CHECK(decoded.ok);
    GRACE_CHECK(decoded.format == SampleFormat::Mono16);
    GRACE_CHECK(decoded.sampleRate == 44100u);
    GRACE_CHECK(grace::audio::frameCount(decoded.samples, decoded.format) ==
                64u);
    GRACE_CHECK(grace::audio::validate(decoded).empty());
}
void testXAudio2Backend(XAudio2AudioBackend& backend)
{
    // "describe names xaudio2
    GRACE_CHECK(backend.describe().find("xaudio2") == 0u);
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
    // "the voice is stopped
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    backend.releaseVoice(voiceId);
    backend.releaseBuffer(bufferId);
    GRACE_CHECK(backend.violationCount() == 0u);  // "the lifecycle is clean
}
void testXAudio2Streaming(XAudio2AudioBackend& backend)
{
    StreamDesc streamDesc;
    streamDesc.format = SampleFormat::Mono16;
    streamDesc.sampleRate = 44100u;
    const std::uint32_t streamId = backend.openStream(streamDesc);
    GRACE_CHECK(streamId != 0u);  // "the stream opens
    // One second of audio: the queue holds the chunk the application
    // appended before the voice existed.
    const std::vector<std::uint8_t> chunk(2u * 44100u, 0u);
    GRACE_CHECK(backend.appendChunk(streamId, chunk));
    GRACE_CHECK(backend.queuedFrames(streamId) == 44100u);
    VoiceDesc desc;
    desc.bufferId = 0u;
    desc.mode = VoiceMode::Sound2D;
    const std::uint32_t voiceId = backend.createStreamVoice(streamId, desc);
    GRACE_CHECK(voiceId != 0u);  // "the stream voice is created
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    backend.play(voiceId);
    backend.mix(1000u);
    // "the stream voice plays from the queue
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Playing);
    backend.stop(voiceId);
    backend.releaseVoice(voiceId);
    backend.releaseStream(streamId);
    // "the streaming lifecycle is clean
    GRACE_CHECK(backend.violationCount() == 0u);
}
void testXAudio2Listener(XAudio2AudioBackend& backend)
{
    grace::audio::ListenerDesc listener;
    listener.position[0] = 1.0f;
    listener.forward[0] = 1.0f;
    backend.updateListener(listener);
    // "the listener update is clean
    GRACE_CHECK(backend.violationCount() == 0u);
}
void testXAudio2Reverb(XAudio2AudioBackend& backend)
{
    SoundDesc sound;
    sound.samples.assign(44100u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    GRACE_CHECK(bufferId != 0u);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    desc.mode = VoiceMode::Sound2D;
    const std::uint32_t voiceId = backend.createVoice(desc);
    GRACE_CHECK(voiceId != 0u);
    ReverbDesc reverb;
    reverb.decay = 1.0f;
    reverb.wetGain = 0.4f;
    backend.setReverb(voiceId, reverb);
    ReverbDesc out;
    GRACE_CHECK(backend.reverb(voiceId, out));  // "the reverb is set
    GRACE_CHECK(out.wetGain == 0.4f);  // "the reverb wet gain round-trips
    backend.clearReverb(voiceId);
    GRACE_CHECK(!backend.reverb(voiceId, out));  // "the reverb is cleared
    backend.releaseVoice(voiceId);
    backend.releaseBuffer(bufferId);
    GRACE_CHECK(backend.violationCount() == 0u);
}
void testXAudio2ForeignHandles(XAudio2AudioBackend& backend)
{
    // "a foreign voice state is stopped
    GRACE_CHECK(backend.state(999u) == VoiceState::Stopped);
    // "a foreign voice release is a violation
    const std::uint64_t before = backend.violationCount();
    backend.releaseVoice(999u);
    GRACE_CHECK(backend.violationCount() == before + 1u);
    // "a foreign buffer release is a violation
    backend.releaseBuffer(999u);
    GRACE_CHECK(backend.violationCount() == before + 2u);
    // "a foreign stream release is a violation
    backend.releaseStream(999u);
    GRACE_CHECK(backend.violationCount() == before + 3u);
}
void testXAudio2InvalidDescriptors(XAudio2AudioBackend& backend)
{
    SoundDesc invalid;
    invalid.samples.clear();
    invalid.format = SampleFormat::Mono16;
    invalid.sampleRate = 44100u;
    const std::uint64_t before = backend.violationCount();
    GRACE_CHECK(backend.createBuffer(invalid) == 0u);
    GRACE_CHECK(backend.violationCount() == before + 1u);
    // "a foreign voice's stream voice is a violation
    VoiceDesc desc;
    desc.bufferId = 0u;
    desc.mode = VoiceMode::Sound2D;
    GRACE_CHECK(backend.createStreamVoice(999u, desc) == 0u);
    GRACE_CHECK(backend.violationCount() == before + 2u);
}
}  // namespace
int main()
{
    XAudio2AudioBackend backend;
    // A device that does not open (no audio hardware, no Wine audio
    // driver) is an environment limit, not a failure: the suite skips
    // with code 77, the same convention the OpenAL suite follows.
    SoundDesc probe;
    probe.samples.assign(4u, 128u);
    probe.format = SampleFormat::Mono8;
    probe.sampleRate = 44100u;
    const std::uint32_t probeId = backend.createBuffer(probe);
    if (probeId == 0u)
    {
        std::fprintf(stderr, "SKIP: no audio device opened\n");
        return 77;
    }
    backend.releaseBuffer(probeId);
    testWavDecode();
    testXAudio2Backend(backend);
    testXAudio2Streaming(backend);
    testXAudio2Listener(backend);
    testXAudio2Reverb(backend);
    testXAudio2ForeignHandles(backend);
    testXAudio2InvalidDescriptors(backend);
    return grace::test::summary("audio_xaudio2");
}
