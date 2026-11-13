#include "grace/core/tween.hpp"

#include <cmath>

namespace grace::core {
namespace {

/// The easing functions, written out so the contract's endpoint rule
/// (f(0) == 0, f(1) == 1) is visible in every branch. The quad and cubic
/// forms are the standard polynomial easings; each in/out/in-out triple
/// shares one polynomial family.
float easeImpl(Ease kind, float t) noexcept
{
    switch (kind)
    {
    case Ease::Linear:
        return t;
    case Ease::QuadIn:
        return t * t;
    case Ease::QuadOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::QuadInOut:
        // Two halves: the in form below the midpoint, the out form above,
        // rescaled so the two meet at 0.5 exactly.
        if (t < 0.5f)
            return 2.0f * t * t;
        {
            const float u = 1.0f - t;
            return 1.0f - 2.0f * u * u;
        }
    case Ease::CubicIn:
        return t * t * t;
    case Ease::CubicOut:
    {
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    case Ease::CubicInOut:
        if (t < 0.5f)
            return 4.0f * t * t * t;
    {
        const float u = 1.0f - t;
        return 1.0f - 4.0f * u * u * u;
    }
    }
    return t;
}

}  // namespace

float ease(Ease kind, float t) noexcept
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    return easeImpl(kind, t);
}

void Tween::start(float from, float to, float durationSeconds, Ease easing)
{
    if (!(durationSeconds > 0.0f) || !std::isfinite(durationSeconds))
        throw std::invalid_argument("Tween: the duration must be a positive finite number");
    m_from = from;
    m_to = to;
    m_duration = durationSeconds;
    m_easing = easing;
    m_elapsed = 0.0f;
    m_progress = 0.0f;
    m_value = from;
    m_finished = false;
}

void Tween::update(float deltaSeconds) noexcept
{
    if (m_finished)
        return;
    m_elapsed += deltaSeconds;
    if (m_elapsed >= m_duration)
    {
        m_elapsed = m_duration;
        m_progress = 1.0f;
        m_value = m_to;
        m_finished = true;
        return;
    }
    m_progress = ease(m_easing, m_elapsed / m_duration);
    m_value = m_from + (m_to - m_from) * m_progress;
}

void Tween2::start(const vec2& from, const vec2& to, float durationSeconds, Ease easing)
{
    m_from = from;
    m_to = to;
    m_value = from;
    m_scalar.start(0.0f, 1.0f, durationSeconds, easing);
}

void Tween2::update(float deltaSeconds) noexcept
{
    m_scalar.update(deltaSeconds);
    m_value = m_from + (m_to - m_from) * m_scalar.value();
}

}  // namespace grace::core
