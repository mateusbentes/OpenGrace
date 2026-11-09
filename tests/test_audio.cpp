// The audio suite: the contract (13a), the null backend (13b) with the
// deterministic mixer, and the WAV decoder (hand-written, no SDK). Every
// check runs against the null backend, which is reproducible bit-for-bit.
// The Ogg Vorbis decoder and the OpenAL adapter live in the adapter
// suite (13c), which the SDK option builds.
//
// Every test gets a fresh backend: the violation counter is cumulative,
// and the exact counts each test expects need a clean instance.
#include "contract_checks.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "grace/audio/audio_decode.hpp"
#include "grace/audio/null_audio_backend.hpp"
namespace {
using grace::audio::AudioBackend;
using grace::audio::AudioStats;
using grace::audio::Decoded;
using grace::audio::ListenerDesc;
using grace::audio::NullAudioBackend;
using grace::audio::ReverbDesc;
using grace::audio::SampleFormat;
using grace::audio::SoundDesc;
using grace::audio::StreamDesc;
using grace::audio::VoiceDesc;
using grace::audio::VoiceMode;
using grace::audio::VoiceState;
void testBackendContract()
{
    NullAudioBackend backend;
    // "describe
    GRACE_CHECK(backend.describe() ==
                "null (headless, deterministic mixer, no device)");
    GRACE_CHECK(backend.capabilityFlags() == 0u);
    GRACE_CHECK(backend.violationCount() == 0u);  // "no violations yet
    GRACE_CHECK(backend.bufferCount() == 0u);  // "no buffers yet
    GRACE_CHECK(backend.voiceCount() == 0u);  // "no voices yet
    const AudioStats stats = backend.stats();
    GRACE_CHECK(stats.bufferCount == 0u);  // "stats has no buffers
    GRACE_CHECK(stats.voiceCount == 0u);  // "stats has no voices
    GRACE_CHECK(stats.playingCount == 0u);  // "stats has no playing voices
    GRACE_CHECK(stats.streamCount == 0u);  // "stats has no streams
    GRACE_CHECK(stats.samplesMixed == 0u);  // "stats has no mixed samples
}
void testBufferValidation()
{
    NullAudioBackend backend;
    SoundDesc invalid;
    invalid.samples = {};
    GRACE_CHECK(backend.createBuffer(invalid) == 0u);
    GRACE_CHECK(backend.violationCount() == 1u);
    invalid.samples = {1u, 2u, 3u};  // not a whole frame
    invalid.sampleRate = 44100u;
    GRACE_CHECK(backend.createBuffer(invalid) == 0u);
    GRACE_CHECK(backend.violationCount() == 2u);
    invalid.samples = {0u, 1u, 2u, 3u};
    invalid.sampleRate = 0u;
    GRACE_CHECK(backend.createBuffer(invalid) == 0u);
    GRACE_CHECK(backend.violationCount() == 3u);
    GRACE_CHECK(backend.bufferCount() == 0u);  // "no buffers were created
    // "validate returns the zero sample rate reason
    GRACE_CHECK(grace::audio::validate(invalid) ==
                "sound has a zero sample rate");
}
void testBufferLifecycle()
{
    NullAudioBackend backend;
    SoundDesc desc;
    desc.samples = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};  // 4 frames of Mono16
    desc.format = SampleFormat::Mono16;
    desc.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(desc);
    GRACE_CHECK(bufferId != 0u);  // "a valid buffer is created
    GRACE_CHECK(backend.bufferCount() == 1u);  // "the buffer count is one
    GRACE_CHECK(grace::audio::frameCount(desc.samples, desc.format) == 4u);
    GRACE_CHECK(grace::audio::bytesPerFrame(SampleFormat::Mono16) == 2u);
    GRACE_CHECK(grace::audio::bytesPerFrame(SampleFormat::Stereo16) == 4u);
    GRACE_CHECK(grace::audio::channelCount(SampleFormat::Stereo8) == 2u);
    backend.releaseBuffer(bufferId);
    GRACE_CHECK(backend.bufferCount() == 0u);  // "the buffer is released
    GRACE_CHECK(backend.violationCount() == 0u);
    backend.releaseBuffer(bufferId);
    GRACE_CHECK(backend.violationCount() == 1u);
}
void testVoiceLifecycle()
{
    NullAudioBackend backend;
    SoundDesc sound;
    sound.samples.assign(44100u, 128u);  // one second of silence
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    desc.mode = VoiceMode::Sound2D;
    desc.gain = 0.5f;
    desc.pitch = 1.0f;
    const std::uint32_t voiceId = backend.createVoice(desc);
    GRACE_CHECK(voiceId != 0u);  // "a valid voice is created
    GRACE_CHECK(backend.voiceCount() == 1u);  // "the voice count is one
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    GRACE_CHECK(backend.cursor(voiceId) == 0u);  // "the voice cursor is at 0
    backend.play(voiceId);
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Playing);
    backend.mix(44100u);  // one second
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    GRACE_CHECK(backend.cursor(voiceId) == 0u);
    const AudioStats stats = backend.stats();
    GRACE_CHECK(stats.samplesMixed == 44100u);  // "the mixed sample count grew
    backend.play(voiceId);
    backend.pause(voiceId);
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Paused);
    backend.mix(1000u);
    GRACE_CHECK(backend.cursor(voiceId) == 0u);
    backend.stop(voiceId);
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    backend.releaseVoice(voiceId);
    GRACE_CHECK(backend.voiceCount() == 0u);  // "the voice is released
    backend.releaseBuffer(bufferId);
    GRACE_CHECK(backend.violationCount() == 0u);  // "the lifecycle is clean
}
void testVoiceLooping()
{
    NullAudioBackend backend;
    SoundDesc sound;
    sound.samples.assign(100u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    desc.loop = true;
    const std::uint32_t voiceId = backend.createVoice(desc);
    backend.play(voiceId);
    backend.mix(250u);
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Playing);
    GRACE_CHECK(backend.cursor(voiceId) == 50u);  // "the cursor wrapped to 50
    backend.releaseVoice(voiceId);
    backend.releaseBuffer(bufferId);
}
void testVoicePitch()
{
    NullAudioBackend backend;
    SoundDesc sound;
    sound.samples.assign(100u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    desc.pitch = 2.0f;
    const std::uint32_t voiceId = backend.createVoice(desc);
    backend.play(voiceId);
    backend.mix(50u);
    // The voice ended, the cursor resets.
    GRACE_CHECK(backend.cursor(voiceId) == 0u);
    // The voice ended at twice the speed.
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    backend.releaseVoice(voiceId);
    backend.releaseBuffer(bufferId);
}
void testVoiceValidation()
{
    NullAudioBackend backend;
    SoundDesc sound;
    sound.samples.assign(10u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    desc.gain = 2.0f;
    GRACE_CHECK(backend.createVoice(desc) == 0u);
    desc.gain = 1.0f;
    desc.pitch = 0.0f;
    GRACE_CHECK(backend.createVoice(desc) == 0u);  // "a zero pitch is rejected
    desc.pitch = 1.0f;
    desc.bufferId = bufferId + 999u;
    GRACE_CHECK(backend.createVoice(desc) == 0u);
    GRACE_CHECK(backend.violationCount() == 3u);
    GRACE_CHECK(backend.voiceCount() == 0u);  // "no voices were created
    VoiceDesc zeroPitch;
    zeroPitch.pitch = 0.0f;
    // The reason is the pitch.
    GRACE_CHECK(grace::audio::validate(zeroPitch) ==
                "voice pitch is not positive");
    backend.releaseBuffer(bufferId);
}
void testVoice3DAttenuation()
{
    NullAudioBackend backend;
    SoundDesc sound;
    sound.samples.assign(44100u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    ListenerDesc listener;
    listener.position[0] = 0.0f;
    listener.position[1] = 0.0f;
    listener.position[2] = 0.0f;
    backend.updateListener(listener);
    // A voice at the reference distance: full gain.
    VoiceDesc near;
    near.bufferId = bufferId;
    near.mode = VoiceMode::Sound3D;
    near.position[0] = 1.0f;  // the reference distance
    const std::uint32_t nearId = backend.createVoice(near);
    // A voice past the max distance: silent.
    VoiceDesc far;
    far.bufferId = bufferId;
    far.mode = VoiceMode::Sound3D;
    far.position[0] = 1000.0f;  // past the max distance of 100.0
    const std::uint32_t farId = backend.createVoice(far);
    backend.play(nearId);
    backend.play(farId);
    const std::uint64_t before = backend.stats().samplesMixed;
    backend.mix(1000u);
    const std::uint64_t after = backend.stats().samplesMixed;
    // Both voices advance; only the near one contributes to the mix.
    GRACE_CHECK(after - before == 1000u);
    // The far voice advances silently.
    GRACE_CHECK(backend.state(farId) == VoiceState::Playing);
    backend.releaseVoice(nearId);
    backend.releaseVoice(farId);
    backend.releaseBuffer(bufferId);
}
void testReverb()
{
    NullAudioBackend backend;
    SoundDesc sound;
    sound.samples.assign(44100u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    const std::uint32_t voiceId = backend.createVoice(desc);
    ReverbDesc out;
    GRACE_CHECK(!backend.reverb(voiceId, out));
    ReverbDesc reverb;
    reverb.decay = 1.5f;
    reverb.damping = 0.25f;
    reverb.wetGain = 0.4f;
    backend.setReverb(voiceId, reverb);
    GRACE_CHECK(backend.reverb(voiceId, out));  // "the reverb is assigned
    GRACE_CHECK(out.decay == 1.5f);  // "the decay round-trips
    GRACE_CHECK(out.damping == 0.25f);  // "the damping round-trips
    GRACE_CHECK(out.wetGain == 0.4f);  // "the wet gain round-trips
    backend.clearReverb(voiceId);
    GRACE_CHECK(!backend.reverb(voiceId, out));  // "the reverb is cleared
    ReverbDesc invalid;
    invalid.wetGain = 2.0f;
    backend.setReverb(voiceId, invalid);
    GRACE_CHECK(backend.violationCount() == 1u);
    GRACE_CHECK(grace::audio::validate(invalid) ==
              "reverb wet gain is outside the unit range");
    backend.releaseVoice(voiceId);
    backend.releaseBuffer(bufferId);
}
void testStreaming()
{
    NullAudioBackend backend;
    StreamDesc streamDesc;
    streamDesc.format = SampleFormat::Mono16;
    streamDesc.sampleRate = 44100u;
    const std::uint32_t streamId = backend.openStream(streamDesc);
    GRACE_CHECK(streamId != 0u);  // "a valid stream is opened
    GRACE_CHECK(backend.queuedFrames(streamId) == 0u);
    // The first chunk: 4 frames of Mono16.
    std::vector<std::uint8_t> chunk = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    GRACE_CHECK(backend.appendChunk(streamId, chunk));
    GRACE_CHECK(backend.queuedFrames(streamId) == 4u);
    GRACE_CHECK(backend.appendChunk(streamId, chunk));
    GRACE_CHECK(backend.queuedFrames(streamId) == 8u);
    // A stream voice plays from the queued chunks.
    VoiceDesc desc;
    desc.bufferId = 0u;
    desc.mode = VoiceMode::Sound2D;
    const std::uint32_t voiceId = backend.createStreamVoice(streamId, desc);
    GRACE_CHECK(voiceId != 0u);  // "a stream voice is created
    backend.play(voiceId);
    backend.mix(8u);
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    GRACE_CHECK(backend.queuedFrames(streamId) == 0u);
    backend.releaseVoice(voiceId);
    backend.releaseStream(streamId);
    GRACE_CHECK(backend.violationCount() == 0u);
    GRACE_CHECK(backend.openStream(streamDesc) != 0u);
    StreamDesc zeroRate;
    zeroRate.sampleRate = 0u;
    // The reason is the rate.
    GRACE_CHECK(grace::audio::validate(zeroRate) ==
                "stream has a zero sample rate");
}
void testStreamValidation()
{
    NullAudioBackend backend;
    StreamDesc invalid;
    invalid.sampleRate = 0u;
    GRACE_CHECK(backend.openStream(invalid) == 0u);
    invalid.sampleRate = 44100u;
    invalid.chunkSamples = 0u;
    GRACE_CHECK(backend.openStream(invalid) == 0u);
    GRACE_CHECK(backend.violationCount() == 2u);
    const std::uint32_t streamId = backend.openStream(StreamDesc{});
    GRACE_CHECK(streamId != 0u);  // "a valid stream opens
    std::vector<std::uint8_t> odd = {1u, 2u, 3u};
    GRACE_CHECK(!backend.appendChunk(streamId, odd));
    backend.releaseStream(streamId);
}
void testForeignHandles()
{
    NullAudioBackend backend;
    GRACE_CHECK(backend.state(999u) == VoiceState::Stopped);
    GRACE_CHECK(backend.cursor(999u) == 0u);  // "a foreign voice cursor is 0
    backend.play(999u);
    backend.pause(999u);
    backend.stop(999u);
    ReverbDesc out;
    GRACE_CHECK(!backend.reverb(999u, out));  // "a foreign voice has no reverb
    backend.clearReverb(999u);
    GRACE_CHECK(backend.queuedFrames(999u) == 0u);
    // play, pause, stop and clearReverb.
    GRACE_CHECK(backend.violationCount() == 4u);
    GRACE_CHECK(backend.stats().voiceCount == 0u);  // "no voices were created
    GRACE_CHECK(backend.stats().streamCount == 0u);
}
void testShutdown()
{
    NullAudioBackend backend;
    SoundDesc sound;
    sound.samples.assign(100u, 128u);
    sound.format = SampleFormat::Mono8;
    sound.sampleRate = 44100u;
    const std::uint32_t bufferId = backend.createBuffer(sound);
    VoiceDesc desc;
    desc.bufferId = bufferId;
    const std::uint32_t voiceId = backend.createVoice(desc);
    backend.play(voiceId);
    backend.shutdown();
    GRACE_CHECK(backend.bufferCount() == 0u);
    GRACE_CHECK(backend.voiceCount() == 0u);
    GRACE_CHECK(backend.state(voiceId) == VoiceState::Stopped);
    backend.shutdown();  // idempotent
    GRACE_CHECK(backend.violationCount() == 0u);
}
void testWavDecode()
{
    // A minimal WAV: 16-bit mono, 4 frames.
    const std::vector<std::uint8_t> wav = {
        // RIFF header
        'R', 'I', 'F', 'F',
        0x24u, 0x00u, 0x00u, 0x00u,  // 36 bytes of payload
        'W', 'A', 'V', 'E',
        // fmt chunk
        'f', 'm', 't', ' ',
        0x10u, 0x00u, 0x00u, 0x00u,  // 16 bytes
        0x01u, 0x00u,                // PCM
        0x01u, 0x00u,                // mono
        0x44u, 0xACu, 0x00u, 0x00u,  // 44100 Hz
        0x88u, 0x58u, 0x01u, 0x00u,  // byte rate
        0x02u, 0x00u,                // block align
        0x10u, 0x00u,                // 16 bits
        // data chunk
        'd', 'a', 't', 'a',
        0x08u, 0x00u, 0x00u, 0x00u,  // 8 bytes
        0x00u, 0x00u, 0x01u, 0x00u, 0x02u, 0x00u, 0x03u, 0x00u};
    const Decoded decoded = grace::audio::decodeWav(wav);
    GRACE_CHECK(decoded.ok);  // "the minimal WAV decodes
    GRACE_CHECK(decoded.format == SampleFormat::Mono16);
    GRACE_CHECK(decoded.sampleRate == 44100u);
    // "the WAV has 4 frames
    GRACE_CHECK(grace::audio::frameCount(decoded.samples, decoded.format) ==
                4u);
    GRACE_CHECK(grace::audio::validate(decoded).empty());
    // A WAV that is not RIFF: the decode rejects it.
    const std::vector<std::uint8_t> notWav = {'N', 'O', 'P', 'E'};
    const Decoded failed = grace::audio::decodeWav(notWav);
    GRACE_CHECK(!failed.ok);  // "a non-WAV payload is rejected
    GRACE_CHECK(failed.reason == "not a RIFF/WAVE file");
    // A stereo 16-bit WAV.
    std::vector<std::uint8_t> stereo = wav;
    stereo[22] = 0x02u;  // stereo
    stereo[32] = 0x04u;  // block align
    stereo[34] = 0x10u;  // 16 bits
    const Decoded stereoDecoded = grace::audio::decodeWav(stereo);
    GRACE_CHECK(stereoDecoded.ok);  // "the stereo WAV decodes
    GRACE_CHECK(stereoDecoded.format == SampleFormat::Stereo16);
}
}  // namespace
int main()
{
    testBackendContract();
    testBufferValidation();
    testBufferLifecycle();
    testVoiceLifecycle();
    testVoiceLooping();
    testVoicePitch();
    testVoiceValidation();
    testVoice3DAttenuation();
    testReverb();
    testStreaming();
    testStreamValidation();
    testForeignHandles();
    testShutdown();
    testWavDecode();
    return grace::test::summary("audio");
}
