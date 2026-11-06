#include "grace/app/application.hpp"

#include <stdexcept>
#include <utility>

namespace grace::app {

Application::Application(ApplicationConfig config, render::RenderBackend& backend,
                         StepCallback on_step)
    : m_config(config),
      m_clock(config.step),
      m_backend(backend),
      m_on_step(std::move(on_step))
{
    if (m_config.frame_ns <= 0)
        throw std::invalid_argument("Application: frame_ns must be positive");
}

bool Application::isValid(const ApplicationConfig& config) noexcept
{
    return config.frame_ns > 0 && core::FixedStepClock::isValid(config.step);
}

void Application::initialise()
{
    if (m_state != State::Created)
        throw std::logic_error("Application::initialise: already initialised");
    m_backend.setClearColor(m_config.clear_color);
    m_state = State::Initialised;
}

std::int64_t Application::runFrame()
{
    if (m_state != State::Initialised)
        throw std::logic_error("Application::runFrame: not initialised");

    const std::int64_t steps = m_clock.advance(m_config.frame_ns);
    const std::uint64_t base =
        m_clock.totalSteps() - static_cast<std::uint64_t>(steps);
    for (std::int64_t i = 0; i < steps; ++i)
    {
        if (m_on_step)
            m_on_step(base + static_cast<std::uint64_t>(i));
    }
    m_backend.clear();
    return steps;
}

std::uint64_t Application::run(std::uint64_t frames)
{
    if (m_state != State::Created)
        throw std::logic_error("Application::run: already ran");

    initialise();
    for (std::uint64_t frame = 0; frame < frames; ++frame)
        static_cast<void>(runFrame());
    shutdown();
    return m_clock.totalSteps();
}

void Application::shutdown()
{
    if (m_state != State::Initialised)
        throw std::logic_error("Application::shutdown: not initialised");
    m_backend.shutdown();
    m_state = State::Shutdown;
}

}  // namespace grace::app
