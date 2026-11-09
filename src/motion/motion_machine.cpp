// The motion module's state machine: the states, the transitions and
// the cross-fade inside one layer.
//
// The layer is the control: the current state plays its clip, the
// transitions fire on a trigger or on the exit time, and the cross-fade
// blends the previous state's pose into the next over the fade
// duration. The poses come from the sampler; the layer blends the
// poses the application passes.
#include "grace/motion/motion_machine.hpp"
#include <algorithm>
#include <cmath>
namespace grace {
namespace motion {
bool MachineDesc::validate(std::string& message) const
{
    if (states.empty())
    {
        message = "a machine's state list is empty";
        return false;
    }
    for (const auto& state : states)
    {
        if (state.name.empty())
        {
            message = "a state's name is empty";
            return false;
        }
    }
    for (std::size_t i = 0; i < transitions.size(); ++i)
    {
        const auto& transition = transitions[i];
        if (transition.from >= states.size()
            || transition.to >= states.size())
        {
            message = "the transition " + std::to_string(i) + ": a "
                      "state index is out of range";
            return false;
        }
        if (transition.fadeDuration < 0.0f)
        {
            message = "the transition " + std::to_string(i) + ": the "
                      "fade duration is negative";
            return false;
        }
    }
    if (initial >= states.size())
    {
        message = "a machine's initial state index is out of range";
        return false;
    }
    return true;
}
struct Layer::Impl
{
    std::vector<StateDesc> states;
    std::vector<TransitionDesc> transitions;
    std::uint32_t current = 0;
    std::uint32_t previous = 0;
    bool fading = false;
    float fadeElapsed = 0.0f;
    float fadeDuration = 0.0f;
    float clipTime = 0.0f;
    /// Enters a state: the previous becomes the fade source, the clip
    /// time restarts, and the fade starts when the duration is
    /// positive.
    void enter(std::uint32_t state, float fade)
    {
        previous = current;
        current = state;
        clipTime = 0.0f;
        if (fade > 0.0f)
        {
            fading = true;
            fadeElapsed = 0.0f;
            fadeDuration = fade;
        }
        else
        {
            fading = false;
        }
    }
};
Layer::Layer() : impl(new Impl)
{
}
Layer::~Layer() = default;
void Layer::setGraph(const MachineDesc& desc)
{
    std::string message;
    if (!desc.validate(message))
    {
        throw MotionError(message);
    }
    impl->states = desc.states;
    impl->transitions = desc.transitions;
    impl->current = desc.initial;
    impl->previous = desc.initial;
    impl->fading = false;
    impl->fadeElapsed = 0.0f;
    impl->fadeDuration = 0.0f;
    impl->clipTime = 0.0f;
}
void Layer::update(float dt)
{
    impl->clipTime += dt;
    if (impl->fading)
    {
        impl->fadeElapsed += dt;
        if (impl->fadeElapsed >= impl->fadeDuration)
        {
            impl->fading = false;
        }
    }
    // The exit-time transitions fire when the layer's clip time passes
    // the source state's exit time (0 disables the rule).
    for (const auto& transition : impl->transitions)
    {
        if (transition.from != impl->current || transition.exitTime <= 0.0f)
        {
            continue;
        }
        if (impl->clipTime >= transition.exitTime)
        {
            impl->enter(transition.to, transition.fadeDuration);
            break;
        }
    }
}
bool Layer::trigger(const std::string& trigger)
{
    for (const auto& transition : impl->transitions)
    {
        if (transition.from != impl->current
            || transition.trigger != trigger)
        {
            continue;
        }
        impl->enter(transition.to, transition.fadeDuration);
        return true;
    }
    return false;
}
std::uint32_t Layer::state() const
{
    return impl->current;
}
std::uint32_t Layer::previousState() const
{
    return impl->previous;
}
bool Layer::isFading() const
{
    return impl->fading;
}
float Layer::clipTime() const
{
    return impl->clipTime;
}
void Layer::blendPose(const std::vector<Pose>& poses, Pose& out) const
{
    if (impl->current >= poses.size())
    {
        throw MotionError("a state index out of range for the poses");
    }
    out = poses[impl->current];
    if (impl->fading && impl->previous < poses.size())
    {
        const float t =
            impl->fadeDuration > 0.0f
                ? std::min(1.0f, impl->fadeElapsed / impl->fadeDuration)
                : 1.0f;
        // The fade blends the previous pose out: at the fade's start
        // the output is the previous pose (t = 0), at its end the
        // current pose (t = 1). blendPose(out, previous, 1 - t)
        // moves `out` towards the previous by 1 - t.
        Sampler::blendPose(out, poses[impl->previous], 1.0f - t);
    }
}
}  // namespace motion
}  // namespace grace
