#pragma once
// The procedural tween (milestone 23): one scalar animated from a start
// value to an end value over a duration, with an easing function shaping
// the progress. Plain data in, plain data out, no callbacks the engine
// would have to keep alive: the application drives update() from its own
// frame step and reads value() — the same ownership the fixed-step clock
// and the curve set carry.
//
// The easing set is the closed set the 2D animation path uses. Every
// function maps t in [0, 1] to [0, 1] and is deterministic: the same
// inputs produce the same output on every platform (no hidden state).
#include <cstdint>
#include <stdexcept>

#include "grace/core/math/vec2.hpp"
#include "grace/core/math/vec4.hpp"

namespace grace::core {

/// The easing functions a tween can carry. Linear is the identity; the
/// quad/cubic pairs come in an in, an out and an in-out form each, which
/// is the set the 2D animation path uses (ease-in for a fall, ease-out
/// for a settle, ease-in-out for a bounce-back).
enum class Ease : std::uint8_t
{
    Linear,
    QuadIn,
    QuadOut,
    QuadInOut,
    CubicIn,
    CubicOut,
    CubicInOut
};

/// Evaluates the easing at t in [0, 1]. Every kind maps the closed unit
/// interval onto itself (the endpoints fixed: f(0) == 0, f(1) == 1), so a
/// tween built on one reports its start value at t 0 and its end value at
/// t 1 exactly.
float ease(Ease kind, float t) noexcept;

/// A scalar tween: start -> end over duration seconds with an easing
/// function shaping the progress. update() advances by the delta seconds
/// the frame step reports; value() reads the current interpolation. The
/// state is three floats and a flag, so a hundred tweens cost nothing and
/// the engine never owns a callback.
class Tween
{
public:
    /// Builds a finished (idle) tween. Its value reads 0 and its progress
    /// reads 1; a caller starts it by calling start().
    Tween() = default;

    /// Starts (or restarts) the tween: from `from` to `to` over
    /// `durationSeconds`, shaped by `easing`. Throws std::invalid_argument
    /// when durationSeconds is not finite or not positive. The tween
    /// starts at `from` (progress 0) and reaches `to` at progress 1.
    void start(float from, float to, float durationSeconds, Ease easing);

    /// Advances by deltaSeconds. Values beyond the duration clamp at the
    /// end value and the tween reports finished() from then on (repeated
    /// update calls after the end are safe and idempotent). A tween that
    /// was never started stays idle: update is a no-op.
    void update(float deltaSeconds) noexcept;

    /// The current interpolated value.
    float value() const noexcept { return m_value; }

    /// The progress in [0, 1]: the eased fraction of the duration elapsed.
    float progress() const noexcept { return m_progress; }

    /// Whether the tween has run to its end (or was never started).
    bool finished() const noexcept { return m_finished; }

    /// Whether start() has been called since construction or the last
    /// completion reset.
    bool running() const noexcept { return !m_finished; }

private:
    float m_value = 0.0f;
    float m_progress = 1.0f;
    float m_from = 0.0f;
    float m_to = 0.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    Ease m_easing = Ease::Linear;
    bool m_finished = true;
};

/// A two-component tween, animated together (a position or a scale): the
/// same duration and easing drive both components, and update() advances
/// both at once. The per-component start and end values are set in start().
class Tween2
{
public:
    Tween2() = default;

    /// Starts both components. Throws std::invalid_argument under the same
    /// rule the scalar start applies.
    void start(const vec2& from, const vec2& to, float durationSeconds, Ease easing);

    void update(float deltaSeconds) noexcept;

    const vec2& value() const noexcept { return m_value; }
    float progress() const noexcept { return m_scalar.progress(); }
    bool finished() const noexcept { return m_scalar.finished(); }
    bool running() const noexcept { return m_scalar.running(); }

private:
    Tween m_scalar;
    vec2 m_from{};
    vec2 m_to{};
    vec2 m_value{};
};

}  // namespace grace::core
