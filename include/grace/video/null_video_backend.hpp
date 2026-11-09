#pragma once
// The null video backend's header: the contract with a deterministic
// frame stepper and fixed playback properties. No SDK, no windowing.
#include <map>
#include <memory>
#include "grace/video/video_backend.hpp"
namespace grace {
namespace video {
/// The contract with a deterministic frame stepper: the playback
/// cursor advances by the delta seconds the update carries, the
/// presented frames accumulate in the stats, and the loop wraps the
/// cursor. No real device is touched, so the evaluation is
/// reproducible on every platform.
class NullVideoBackend : public VideoBackend
{
public:
    NullVideoBackend();
    ~NullVideoBackend() override;
    std::string describe() const override;
    void shutdown() override;
    std::uint64_t violationCount() const override;
    std::uint32_t createVideo(const VideoDesc& desc) override;
    void releaseVideo(std::uint32_t videoId) override;
    std::uint32_t videoCount() const override;
    std::uint32_t createPlayback(const PlaybackDesc& desc) override;
    void releasePlayback(std::uint32_t playbackId) override;
    void play(std::uint32_t playbackId) override;
    void pause(std::uint32_t playbackId) override;
    void stop(std::uint32_t playbackId) override;
    PlaybackState state(std::uint32_t playbackId) const override;
    std::uint32_t cursor(std::uint32_t playbackId) const override;
    std::uint32_t frameCount(std::uint32_t playbackId) const override;
    void update(float deltaSeconds) override;
    VideoStats stats() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace video
}  // namespace grace
