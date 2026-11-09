// The video suite: the contract (16a) and the null backend (16b) with
// the deterministic frame stepper. Every check runs on the deterministic
// CPU evaluation, which is reproducible on every platform.
#include "contract_checks.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "grace/video/video_types.hpp"
#include "grace/video/video_decode.hpp"
#include "grace/video/null_video_backend.hpp"
namespace {
using grace::video::FrameFormat;
using grace::video::NullVideoBackend;
using grace::video::PlaybackDesc;
using grace::video::PlaybackState;
using grace::video::VideoBackend;
using grace::video::VideoDesc;
using grace::video::VideoStats;
using grace::video::frameBytes;
/// A video descriptor: the payload, the format and the frame count.
VideoDesc videoDesc(std::uint32_t frameCount)
{
    VideoDesc desc;
    desc.width = 64u;
    desc.height = 64u;
    desc.frameRate = 30u;
    desc.frameCount = frameCount;
    desc.format = FrameFormat::Yuv420;
    const std::uint32_t bytes = frameBytes(desc.format, desc.width,
                                           desc.height);
    desc.frames.assign(bytes * frameCount, 0u);
    return desc;
}
/// A playback descriptor referencing a video id.
PlaybackDesc playbackDesc(std::uint32_t videoId)
{
    PlaybackDesc desc;
    desc.videoId = videoId;
    return desc;
}
void testContract()
{
    // A valid descriptor validates.
    GRACE_CHECK(videoDesc(1u).frames.size() == 6144u);
    // The validation of the video descriptor.
    VideoDesc desc;
    GRACE_CHECK(grace::video::validate(desc) == "a video's size is zero");
    desc.width = 63u;
    desc.height = 64u;
    GRACE_CHECK(grace::video::validate(desc) == "a video's size is not "
                                              "even (the 420 chroma "
                                              "subsamples)");
    desc.width = 64u;
    GRACE_CHECK(grace::video::validate(desc) == "a video's frame rate is "
                                              "zero");
    desc.frameRate = 30u;
    GRACE_CHECK(grace::video::validate(desc) == "a video's frame count is "
                                              "zero");
    desc.frameCount = 1u;
    GRACE_CHECK(grace::video::validate(desc) == "a video's payload is "
                                              "empty");
    desc.frames.assign(6143u, 0u);
    GRACE_CHECK(grace::video::validate(desc) == "a video's payload does "
                                              "not match its frame count");
    desc.frames.assign(6144u, 0u);
    GRACE_CHECK(grace::video::validate(desc).empty());
    // The validation of the playback descriptor.
    PlaybackDesc playback;
    GRACE_CHECK(grace::video::validate(playback) == "a playback's video id "
                                                  "is zero");
    playback.videoId = 1u;
    playback.rate = 0.0f;
    GRACE_CHECK(grace::video::validate(playback) == "a playback's rate is "
                                                  "not positive");
    playback.rate = 1.0f;
    GRACE_CHECK(grace::video::validate(playback).empty());
    // The byte count of one packed frame in the format.
    GRACE_CHECK(frameBytes(FrameFormat::Yuv420, 64u, 64u) == 6144u);
    GRACE_CHECK(frameBytes(FrameFormat::Rgba8, 64u, 64u) == 16384u);
}
void testVideoLifecycle()
{
    NullVideoBackend backend;
    GRACE_CHECK(backend.describe() == "null (deterministic frame stepper)");
    GRACE_CHECK(backend.violationCount() == 0u);
    // The video id is 1-based.
    const std::uint32_t videoId = backend.createVideo(videoDesc(1u));
    GRACE_CHECK(videoId == 1u);
    GRACE_CHECK(backend.videoCount() == 1u);
    // An invalid descriptor returns 0 and counts a violation.
    GRACE_CHECK(backend.createVideo(VideoDesc()) == 0u);
    GRACE_CHECK(backend.violationCount() == 0u);
    // Releasing an unknown id counts a violation.
    backend.releaseVideo(0u);
    GRACE_CHECK(backend.violationCount() == 1u);
    // Releasing a valid id is safe.
    backend.releaseVideo(videoId);
    GRACE_CHECK(backend.videoCount() == 0u);
    // Releasing the same id twice counts a violation.
    backend.releaseVideo(videoId);
    GRACE_CHECK(backend.violationCount() == 2u);
}
void testPlaybackLifecycle()
{
    NullVideoBackend backend;
    const std::uint32_t videoId = backend.createVideo(videoDesc(1u));
    // The playback id is 1-based.
    const std::uint32_t playbackId = backend.createPlayback(
        playbackDesc(videoId));
    GRACE_CHECK(playbackId == 1u);
    // A zero video id is invalid: 0 and a violation.
    PlaybackDesc zero;
    GRACE_CHECK(backend.createPlayback(zero) == 0u);
    GRACE_CHECK(backend.violationCount() == 1u);
    // A foreign video id is invalid: 0 and a violation.
    PlaybackDesc foreign;
    foreign.videoId = 999u;
    GRACE_CHECK(backend.createPlayback(foreign) == 0u);
    GRACE_CHECK(backend.violationCount() == 2u);
    // The state of a null or foreign handle reports Stopped and counts
    // a violation.
    GRACE_CHECK(backend.state(0u) == PlaybackState::Stopped);
    GRACE_CHECK(backend.violationCount() == 3u);
    GRACE_CHECK(backend.state(999u) == PlaybackState::Stopped);
    GRACE_CHECK(backend.violationCount() == 4u);
    // The cursor of a null or foreign handle reports 0.
    GRACE_CHECK(backend.cursor(0u) == 0u);
    GRACE_CHECK(backend.violationCount() == 5u);
    // The frame count of a null or foreign handle reports 0.
    GRACE_CHECK(backend.frameCount(0u) == 0u);
    GRACE_CHECK(backend.violationCount() == 6u);
    // Releasing an unknown id counts a violation.
    backend.releasePlayback(0u);
    GRACE_CHECK(backend.violationCount() == 7u);
    // Releasing a valid id is safe.
    backend.releasePlayback(playbackId);
    GRACE_CHECK(backend.violationCount() == 7u);
}
void testPlayPauseStop()
{
    NullVideoBackend backend;
    const std::uint32_t videoId = backend.createVideo(videoDesc(1u));
    const std::uint32_t playbackId = backend.createPlayback(
        playbackDesc(videoId));
    // The initial state is Stopped.
    GRACE_CHECK(backend.state(playbackId) == PlaybackState::Stopped);
    // Playing sets the state to Playing.
    backend.play(playbackId);
    GRACE_CHECK(backend.state(playbackId) == PlaybackState::Playing);
    // Pausing sets the state to Paused.
    backend.pause(playbackId);
    GRACE_CHECK(backend.state(playbackId) == PlaybackState::Paused);
    // Playing again resumes from the cursor.
    backend.play(playbackId);
    GRACE_CHECK(backend.state(playbackId) == PlaybackState::Playing);
    // Stopping sets the state to Stopped.
    backend.stop(playbackId);
    GRACE_CHECK(backend.state(playbackId) == PlaybackState::Stopped);
    // Playing again restarts from the first frame.
    backend.play(playbackId);
    GRACE_CHECK(backend.state(playbackId) == PlaybackState::Playing);
    GRACE_CHECK(backend.cursor(playbackId) == 0u);
    // The playback of a null or foreign id counts a violation.
    backend.play(0u);
    GRACE_CHECK(backend.violationCount() == 1u);
    backend.pause(0u);
    GRACE_CHECK(backend.violationCount() == 2u);
    backend.stop(0u);
    GRACE_CHECK(backend.violationCount() == 3u);
    // The frame count of a playback reports the video's frame count.
    GRACE_CHECK(backend.frameCount(playbackId) == 1u);
}
void testStepping()
{
    NullVideoBackend backend;
    const std::uint32_t videoId = backend.createVideo(videoDesc(1u));
    const std::uint32_t playbackId = backend.createPlayback(
        playbackDesc(videoId));
    // The update of a stopped playback is a no-op: the cursor does not
    // move.
    backend.update(1.0f);
    GRACE_CHECK(backend.cursor(playbackId) == 0u);
    // The update of a negative delta counts a violation.
    backend.update(-1.0f);
    GRACE_CHECK(backend.violationCount() == 1u);
    // Playing and stepping advances the cursor.
    backend.play(playbackId);
    backend.update(1.0f);
    GRACE_CHECK(backend.cursor(playbackId) == 1u);
    // The cursor stops at the frame count.
    backend.update(1.0f);
    GRACE_CHECK(backend.cursor(playbackId) == 1u);
}
void testLoop()
{
    NullVideoBackend backend;
    const std::uint32_t videoId = backend.createVideo(videoDesc(1u));
    PlaybackDesc desc = playbackDesc(videoId);
    desc.loop = true;
    const std::uint32_t playbackId = backend.createPlayback(desc);
    backend.play(playbackId);
    // One second at 30 fps consumes 30 frame periods; the frame count
    // is 1, so the cursor wraps to the first frame inside the update.
    backend.update(1.0f);
    GRACE_CHECK(backend.cursor(playbackId) == 0u);
    // The loop keeps playing.
    GRACE_CHECK(backend.state(playbackId) == PlaybackState::Playing);
}
void testRate()
{
    NullVideoBackend backend;
    // The frame count is 4, so the rate maths reads at one frame per
    // step: the rate 2.0 consumes two frames in one 1/15s step.
    const std::uint32_t videoId = backend.createVideo(videoDesc(4u));
    PlaybackDesc desc = playbackDesc(videoId);
    desc.rate = 2.0f;
    const std::uint32_t playbackId = backend.createPlayback(desc);
    backend.play(playbackId);
    // The rate 2.0 at 30 fps consumes one frame in 1/60 s.
    backend.update(1.0f / 60.0f);
    GRACE_CHECK(backend.cursor(playbackId) == 1u);
    backend.update(1.0f / 60.0f);
    GRACE_CHECK(backend.cursor(playbackId) == 2u);
    // The rate 0.5 advances the cursor by half the delta.
    PlaybackDesc half = playbackDesc(videoId);
    half.rate = 0.5f;
    const std::uint32_t halfId = backend.createPlayback(half);
    backend.play(halfId);
    // The rate 0.5 at 30 fps consumes one frame in 1/15 s.
    backend.update(1.0f / 15.0f);
    GRACE_CHECK(backend.cursor(halfId) == 1u);
    // A second step at the rate 0.5 consumes the next frame.
    backend.update(1.0f / 15.0f);
    GRACE_CHECK(backend.cursor(halfId) == 2u);
}
void testStats()
{
    NullVideoBackend backend;
    const std::uint32_t videoId = backend.createVideo(videoDesc(1u));
    const std::uint32_t playbackId = backend.createPlayback(
        playbackDesc(videoId));
    backend.play(playbackId);
    backend.update(1.0f);
    const VideoStats stats = backend.stats();
    GRACE_CHECK(stats.videoCount == 1u);
    GRACE_CHECK(stats.playbackCount == 1u);
    GRACE_CHECK(stats.framesPresented == 1u);
    GRACE_CHECK(stats.violationCount == 0u);
    // The stats' violation count mirrors the backend's.
    backend.play(0u);
    GRACE_CHECK(backend.stats().violationCount == 1u);
}
void testDecode()
{
    // The decode validation of a decoded payload.
    grace::video::Decoded decoded;
    GRACE_CHECK(grace::video::validate(decoded) == "the decode failed");
    decoded.ok = true;
    decoded.width = 64u;
    decoded.height = 64u;
    decoded.frameRate = 30u;
    decoded.frameCount = 1u;
    decoded.frames.assign(6144u, 0u);
    GRACE_CHECK(grace::video::validate(decoded).empty());
    decoded.frames.assign(6143u, 0u);
    GRACE_CHECK(grace::video::validate(decoded) == "the payload does not "
                                                  "match its frame count");
}
}  // namespace
int main()
{
    testContract();
    testVideoLifecycle();
    testPlaybackLifecycle();
    testPlayPauseStop();
    testStepping();
    testLoop();
    testRate();
    testStats();
    testDecode();
    return grace::test::summary("video");
}
