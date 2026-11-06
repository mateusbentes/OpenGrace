#pragma once

#include <cstdint>
#include <functional>

#include "grace/core/fixed_step_clock.hpp"
#include "grace/render/renderer.hpp"

namespace grace::app {

/// Configuration of an Application run.
struct ApplicationConfig
{
    std::int64_t frame_ns = 0;  ///< Simulated wall-clock time per frame; must be > 0.
    core::FixedStepConfig step{};  ///< Fixed-step clock configuration.
    /// Colour applied at initialise and used by every frame.
    render::ClearColor clear_color{0, 0, 0, 255};
};

/// Application lifecycle driver (App module).
///
/// Phases: initialise() applies the clear colour; each frame advances the
/// fixed-step clock by frame_ns, executes the due fixed steps through the step
/// callback and renders exactly one frame through the renderer
/// (beginFrame/endFrame); shutdown() shuts the backend down. The driver
/// depends only on a core::FixedStepClock and a reference to a
/// render::Renderer — the renderer owns frame composition, the application
/// owns the loop (milestone 5b layering, decision 12).
class Application
{
public:
    /// Called once per executed fixed step, with the global step index.
    using StepCallback = std::function<void(std::uint64_t step_index)>;

    /// Throws std::invalid_argument when the configuration is invalid.
    Application(ApplicationConfig config, render::Renderer& renderer,
                StepCallback on_step = StepCallback{});

    /// True when frame_ns > 0 and the step configuration is valid.
    static bool isValid(const ApplicationConfig& config) noexcept;

    /// Applies the clear colour. Exactly once per run.
    void initialise();

    /// Advances the clock by one frame, executes the due fixed steps through
    /// the step callback, then renders exactly one frame through the renderer.
    /// Returns the number of steps executed. Requires initialise().
    std::int64_t runFrame();

    /// Full lifecycle: initialise(), frames x runFrame(), shutdown().
    /// Returns the total number of executed steps.
    std::uint64_t run(std::uint64_t frames);

    /// Shuts the backend down. Requires initialise(), at most once.
    void shutdown();

    const core::FixedStepClock& clock() const noexcept { return m_clock; }

private:
    enum class State
    {
        Created,
        Initialised,
        Shutdown
    };

    ApplicationConfig m_config;
    core::FixedStepClock m_clock;
    render::Renderer& m_renderer;
    StepCallback m_on_step;
    State m_state = State::Created;
};

}  // namespace grace::app
