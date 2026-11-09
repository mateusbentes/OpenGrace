#pragma once
// The motion module's state machine: the layers, the states, the
// transitions and the controllers (docs/grace-architecture.md section
// 3.7).
//
// A layer is one state machine over one clip set: the current state
// plays its clip, the transitions fire on a trigger or on the source
// state's exit time, and the cross-fade blends the previous state's
// pose into the next over the fade duration. The documented
// architecture's eight layers are the application's composition: the
// machine is per-layer, and the application stacks the layers it needs
// and blends their poses itself (or with the Machine helper below).
// Nothing here names a graphics API, a physics SDK or a scripting
// language.
#include <memory>
#include "grace/motion/motion_sampler.hpp"
namespace grace {
namespace motion {
/// A state machine's state: the name, the clip it plays, the playback
/// speed and whether the clip loops.
struct StateDesc final
{
    std::string name;
    std::uint32_t clipId = 0;
    float speed = 1.0f;
    bool loop = false;
};
/// A transition: the state it leaves, the state it enters, the trigger
/// that fires it, the exit time in the source state's clip seconds (0
/// means the trigger alone fires it) and the cross-fade duration in
/// seconds.
struct TransitionDesc final
{
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    std::string trigger;
    float exitTime = 0.0f;
    float fadeDuration = 0.0f;
};
/// A layer's graph description: the states, the transitions and the
/// initial state.
struct MachineDesc final
{
    std::vector<StateDesc> states;
    std::vector<TransitionDesc> transitions;
    std::uint32_t initial = 0;
    bool validate(std::string& message) const;
};
/// A layer: one state machine over one clip set. The operations are
/// deterministic.
class Layer
{
public:
    Layer();
    ~Layer();
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;
    /// Sets the graph. Throws MotionError on an invalid graph (an empty
    /// state list, a transition naming an unknown state, a state index
    /// out of range).
    void setGraph(const MachineDesc& desc);
    /// Advances the layer: the clip time advances by dt (the state's
    /// speed applies at the sampler call), the exit-time transitions
    /// fire, and the cross-fade advances. The dt is the application's
    /// fixed step, not scaled.
    void update(float dt);
    /// Fires a trigger: the transition whose source is the current
    /// state and whose trigger is the name fires. Returns whether one
    /// fired.
    bool trigger(const std::string& trigger);
    /// The current state's index.
    std::uint32_t state() const;
    /// The state the cross-fade blends from (the current state while no
    /// fade runs).
    std::uint32_t previousState() const;
    /// Whether a cross-fade is running.
    bool isFading() const;
    /// The layer's clip time (the state machine's own clock, not the
    /// clip's wrapped time; the sampler wraps it).
    float clipTime() const;
    /// Blends the layer's poses into one: the current state's pose, and
    /// the previous state's pose blended out by the cross-fade's
    /// progress. The poses are the states' evaluated poses, in the
    /// graph's state order. Throws MotionError on a pose list smaller
    /// than the current state's index.
    void blendPose(const std::vector<Pose>& poses, Pose& out) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace motion
}  // namespace grace
