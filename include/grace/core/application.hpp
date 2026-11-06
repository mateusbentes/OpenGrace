#pragma once

#include <cstdint>
#include <functional>

#include "grace/core/fixed_step_clock.hpp"
#include "grace/render/render_backend.hpp"

namespace grace::core {

/// Configuration of an Application run.
struct ApplicationConfig
{
    std::int64_t frame_ns = 0;  ///< Simulated wall-clock time per frame; must be > 0.
    FixedStepConfig step{};     ///< Fixed-step clock configuration.
    /// Colour applied to the backend at initialise and used by every frame.
    render::ClearColor clear_color{0, 0, 0, 255};
};

/// Application lifecycle driver.
///
/// Phases: initialise() applies the clear colour to the backend; each frame
/// advances the fixed-step clock by frame_ns, executes the due fixed steps
/// through the step callback and renders exactly one frame (one backend
/// clear); shutdown() shuts the backend down. The driver depends only on a
/// FixedStepClock and a reference to a render::RenderBackend.
class Application
{
public:
    /// Called once per executed fixed step, with the global step index.
    using StepCallback = std::function<void(std::uint64_t step_index)>;

    /// Throws std::invalid_argument when the configuration is invalid.
    Application(ApplicationConfig config, render::RenderBackend& backend,
                StepCallback on_step = StepCallback{});

    /// True when frame_ns > 0 and the step configuration is valid.
    static bool isValid(const ApplicationConfig& config) noexcept;

    /// Applies the clear colour to the backend. Exactly once per run.
    void initialise();

    /// Advances the clock by one frame, executes the due fixed steps through
    /// the step callback, then renders exactly one frame (backend clear).
    /// Returns the number of steps executed. Requires initialise().
    std::int64_t runFrame();

    /// Full lifecycle: initialise(), frames x runFrame(), shutdown().
    /// Returns the total number of executed steps.
    std::uint64_t run(std::uint64_t frames);

    /// Shuts the backend down. Requires initialise(), at most once.
    void shutdown();

    const FixedStepClock& clock() const noexcept { return m_clock; }

private:
    enum class State
    {
        Created,
        Initialised,
        Shutdown
    };

    ApplicationConfig m_config;
    FixedStepClock m_clock;
    render::RenderBackend& m_backend;
    StepCallback m_on_step;
    State m_state = State::Created;
};

}  // namespace grace::core
