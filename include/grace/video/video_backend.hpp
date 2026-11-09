#pragma once
// The video module's contract: the backend interface the application
// programs against (docs/grace-architecture.md section 2's module
// table). One class, plain data in and out, never throws.
//
// The contract is the protocol reference for every backend: the null
// backend (16b) implements it with a deterministic frame stepper, and
// the Theora adapter (16c) forwards the calls. The unit of playback is
// a playback; the unit of data is a video. The frames presented are the
// application's to draw: the backend never touches the render contract.
#include <cstdint>
#include <string>
#include "grace/video/video_types.hpp"
namespace grace {
namespace video {
/// Opaque video handle. The backend owns the objects; the application
/// moves the handles.
class Video;
/// Opaque playback handle. The backend owns the objects; the
/// application moves the handles.
class Playback;
/// The video backend contract: upload videos, drive playbacks, step in
/// the update. Every call is safe on a null or foreign handle; the
/// misuses bump a violation counter the application can read.
class VideoBackend
{
public:
    VideoBackend() = default;
    virtual ~VideoBackend() = default;
    VideoBackend(const VideoBackend&) = delete;
    VideoBackend& operator=(const VideoBackend&) = delete;
    VideoBackend(VideoBackend&&) = delete;
    VideoBackend& operator=(VideoBackend&&) = delete;
    /// A human-readable description of the backend, for diagnostics.
    virtual std::string describe() const = 0;
    /// Releases every backend resource. Idempotent.
    virtual void shutdown() = 0;
    /// The number of protocol violations since the start: the misuses
    /// the application can detect (a null handle, a foreign handle).
    virtual std::uint64_t violationCount() const = 0;
    // -- Videos -----------------------------------------
    //
    // A video is a decoded frame payload the application uploads once.
    /// Uploads a video descriptor. Returns the video id (1-based) for
    /// the application to reference it; 0 on an invalid descriptor.
    virtual std::uint32_t createVideo(const VideoDesc& desc) = 0;
    /// Releases a video. Releasing an unknown id is a protocol
    /// violation. A playback that references the video is stopped
    /// first, and the video id is not reused by the backend.
    virtual void releaseVideo(std::uint32_t videoId) = 0;
    /// The video count currently alive.
    virtual std::uint32_t videoCount() const = 0;
    // -- Playbacks ---------------------------------------
    //
    // A playback is one playback instance of one video. The playback
    // carries the loop and the rate.
    /// Creates a playback. Returns the playback id (1-based); 0 on an
    /// invalid descriptor.
    virtual std::uint32_t createPlayback(const PlaybackDesc& desc) = 0;
    /// Releases a playback. Releasing an unknown id is a protocol
    /// violation.
    virtual void releasePlayback(std::uint32_t playbackId) = 0;
    /// Starts a playback. A stopped playback restarts from the first
    /// frame; a paused one resumes; a playing one is a no-op.
    virtual void play(std::uint32_t playbackId) = 0;
    /// Pauses a playback. A paused playback holds its cursor; a
    /// stopped playback is a no-op.
    virtual void pause(std::uint32_t playbackId) = 0;
    /// Stops a playback. A stopped playback holds its cursor; a
    /// playing one is a no-op.
    virtual void stop(std::uint32_t playbackId) = 0;
    /// The state of a playback. A null or foreign handle reports
    /// Stopped and counts a violation.
    virtual PlaybackState state(std::uint32_t playbackId) const = 0;
    /// The frame index a playback holds. A null or foreign handle
    /// reports 0 and counts a violation.
    virtual std::uint32_t cursor(std::uint32_t playbackId) const = 0;
    /// The frame count of a playback's video. A null or foreign handle
    /// reports 0 and counts a violation.
    virtual std::uint32_t frameCount(std::uint32_t playbackId) const = 0;
    // -- Update ------------------------------------------
    //
    /// Advances the playback by the frame the delta seconds imply,
    /// presenting the frame at the rate. The delta must not be
    /// negative; a negative delta is a protocol violation.
    virtual void update(float deltaSeconds) = 0;
    /// The backend's statistics.
    virtual VideoStats stats() const = 0;
};
}  // namespace video
}  // namespace grace
