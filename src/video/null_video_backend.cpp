// The null video backend's implementation: the contract with a
// deterministic frame stepper. No real device is touched: the playback
// cursor advances by the delta seconds the update carries, the
// presented frames accumulate in the stats, and the loop wraps the
// cursor. The evaluation is reproducible on every platform.
#include "grace/video/null_video_backend.hpp"
#include <algorithm>
#include <map>
#include <vector>
namespace grace {
namespace video {
namespace {
/// The video record: the payload the application uploaded, the frame
/// properties and the payload's byte count.
struct VideoRecord
{
    std::vector<std::uint8_t> frames;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frameRate = 0;
    std::uint32_t frameCount = 0;
};
/// The playback record: the video it references and its playback
/// properties.
struct PlaybackRecord
{
    std::uint32_t videoId = 0;
    bool loop = false;
    float rate = 1.0f;
    PlaybackState state = PlaybackState::Stopped;
    std::uint32_t cursor = 0;
    // The fraction of a frame the update carried but the cursor did not
    // consume, so the cursor advances deterministically.
    float frameFraction = 0.0f;
};
}  // namespace
struct NullVideoBackend::Impl
{
    std::map<std::uint32_t, VideoRecord> videos;
    std::map<std::uint32_t, PlaybackRecord> playbacks;
    std::uint32_t nextVideoId = 1u;
    std::uint32_t nextPlaybackId = 1u;
    std::uint64_t violations = 0u;
    std::uint64_t framesPresented = 0u;
    bool shutdown = false;
    bool isVideo(std::uint32_t videoId) const
    {
        return videos.find(videoId) != videos.end();
    }
    bool isPlayback(std::uint32_t playbackId) const
    {
        return playbacks.find(playbackId) != playbacks.end();
    }
};
NullVideoBackend::NullVideoBackend() : impl(std::make_unique<Impl>()) {}
NullVideoBackend::~NullVideoBackend() = default;
std::string NullVideoBackend::describe() const
{
    return "null (deterministic frame stepper)";
}
void NullVideoBackend::shutdown()
{
    impl->videos.clear();
    impl->playbacks.clear();
    impl->shutdown = true;
}
std::uint64_t NullVideoBackend::violationCount() const
{
    return impl->violations;
}
std::uint32_t NullVideoBackend::createVideo(const VideoDesc& desc)
{
    if (!validate(desc).empty())
    {
        return 0u;
    }
    VideoRecord record;
    record.frames = desc.frames;
    record.width = desc.width;
    record.height = desc.height;
    record.frameRate = desc.frameRate;
    record.frameCount = desc.frameCount;
    const std::uint32_t id = impl->nextVideoId++;
    impl->videos.emplace(id, std::move(record));
    return id;
}
void NullVideoBackend::releaseVideo(std::uint32_t videoId)
{
    if (!impl->isVideo(videoId))
    {
        ++impl->violations;
        return;
    }
    // A playback that references the video is stopped first, the same
    // rule the audio module applies to a buffer release.
    for (auto& entry : impl->playbacks)
    {
        if (entry.second.videoId == videoId)
        {
            entry.second.state = PlaybackState::Stopped;
            entry.second.cursor = 0u;
        }
    }
    impl->videos.erase(videoId);
}
std::uint32_t NullVideoBackend::videoCount() const
{
    return static_cast<std::uint32_t>(impl->videos.size());
}
std::uint32_t NullVideoBackend::createPlayback(const PlaybackDesc& desc)
{
    if (!validate(desc).empty() || !impl->isVideo(desc.videoId))
    {
        ++impl->violations;
        return 0u;
    }
    PlaybackRecord record;
    record.videoId = desc.videoId;
    record.loop = desc.loop;
    record.rate = desc.rate;
    const std::uint32_t id = impl->nextPlaybackId++;
    impl->playbacks.emplace(id, std::move(record));
    return id;
}
void NullVideoBackend::releasePlayback(std::uint32_t playbackId)
{
    if (!impl->isPlayback(playbackId))
    {
        ++impl->violations;
        return;
    }
    impl->playbacks.erase(playbackId);
}
void NullVideoBackend::play(std::uint32_t playbackId)
{
    if (!impl->isPlayback(playbackId))
    {
        ++impl->violations;
        return;
    }
    PlaybackRecord& playback = impl->playbacks[playbackId];
    if (playback.state == PlaybackState::Stopped)
    {
        playback.cursor = 0u;
        playback.frameFraction = 0.0f;
    }
    playback.state = PlaybackState::Playing;
}
void NullVideoBackend::pause(std::uint32_t playbackId)
{
    if (!impl->isPlayback(playbackId))
    {
        ++impl->violations;
        return;
    }
    PlaybackRecord& playback = impl->playbacks[playbackId];
    if (playback.state == PlaybackState::Playing)
    {
        playback.state = PlaybackState::Paused;
    }
}
void NullVideoBackend::stop(std::uint32_t playbackId)
{
    if (!impl->isPlayback(playbackId))
    {
        ++impl->violations;
        return;
    }
    PlaybackRecord& playback = impl->playbacks[playbackId];
    if (playback.state == PlaybackState::Playing)
    {
        playback.state = PlaybackState::Stopped;
    }
}
PlaybackState NullVideoBackend::state(std::uint32_t playbackId) const
{
    if (!impl->isPlayback(playbackId))
    {
        ++impl->violations;
        return PlaybackState::Stopped;
    }
    return impl->playbacks.at(playbackId).state;
}
std::uint32_t NullVideoBackend::cursor(std::uint32_t playbackId) const
{
    if (!impl->isPlayback(playbackId))
    {
        ++impl->violations;
        return 0u;
    }
    return impl->playbacks.at(playbackId).cursor;
}
std::uint32_t NullVideoBackend::frameCount(std::uint32_t playbackId) const
{
    if (!impl->isPlayback(playbackId))
    {
        ++impl->violations;
        return 0u;
    }
    const std::uint32_t videoId = impl->playbacks.at(playbackId).videoId;
    return impl->videos.at(videoId).frameCount;
}
void NullVideoBackend::update(float deltaSeconds)
{
    if (deltaSeconds < 0.0f)
    {
        ++impl->violations;
        return;
    }
    for (auto& entry : impl->playbacks)
    {
        PlaybackRecord& playback = entry.second;
        if (playback.state != PlaybackState::Playing)
        {
            continue;
        }
        const VideoRecord& video = impl->videos[playback.videoId];
        // The fraction of a frame the delta seconds implies, at the
        // video's own rate scaled by the playback's rate.
        const float framesAhead =
            static_cast<float>(video.frameRate) * deltaSeconds * playback.rate;
        playback.frameFraction += framesAhead;
        while (playback.frameFraction >= 1.0f)
        {
            playback.frameFraction -= 1.0f;
            ++playback.cursor;
            ++impl->framesPresented;
            if (playback.cursor < video.frameCount)
            {
                continue;
            }
            if (playback.loop)
            {
                // The cursor wraps to the first frame.
                playback.cursor = 0u;
            }
            else
            {
                playback.state = PlaybackState::Stopped;
                playback.frameFraction = 0.0f;
                break;
            }
        }
    }
}
VideoStats NullVideoBackend::stats() const
{
    VideoStats stats;
    stats.videoCount = static_cast<std::uint32_t>(impl->videos.size());
    stats.playbackCount = static_cast<std::uint32_t>(impl->playbacks.size());
    stats.framesPresented = impl->framesPresented;
    stats.violationCount = impl->violations;
    return stats;
}
}  // namespace video
}  // namespace grace
