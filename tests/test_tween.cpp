#include "grace/core/tween.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::Ease;
using grace::core::Tween;
using grace::core::Tween2;
using grace::core::ease;
using grace::core::vec2;

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

bool nearVec(const vec2& a, const vec2& b, float eps)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps);
}

template <typename Start>
bool throwsInvalidArgument(Start&& start)
{
    try
    {
        start();
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    // The endpoints are fixed for every easing: f(0) == 0 and f(1) == 1, so
    // a tween built on one reports its start value at t 0 and its end value
    // at t 1 exactly.
    const std::vector<Ease> kinds{
        Ease::Linear,  Ease::QuadIn,    Ease::QuadOut,
        Ease::QuadInOut, Ease::CubicIn, Ease::CubicOut,
        Ease::CubicInOut,
    };
    for (const Ease kind : kinds)
    {
        GRACE_CHECK_EQ(ease(kind, 0.0f), 0.0f);
        GRACE_CHECK_EQ(ease(kind, 1.0f), 1.0f);
    }
    // The known intermediate values: the quad and cubic polynomials have
    // closed forms, so half-way and quarter-way points are exact.
    GRACE_CHECK_EQ(ease(Ease::Linear, 0.25f), 0.25f);
    GRACE_CHECK(near(ease(Ease::QuadIn, 0.5f), 0.25f, 1e-6f));
    GRACE_CHECK(near(ease(Ease::QuadOut, 0.5f), 0.75f, 1e-6f));
    GRACE_CHECK(near(ease(Ease::QuadInOut, 0.5f), 0.5f, 1e-6f));
    GRACE_CHECK(near(ease(Ease::CubicIn, 0.5f), 0.125f, 1e-6f));
    GRACE_CHECK(near(ease(Ease::CubicOut, 0.5f), 0.875f, 1e-6f));
    GRACE_CHECK(near(ease(Ease::CubicInOut, 0.5f), 0.5f, 1e-6f));
    // Values outside [0, 1] clamp at the endpoints.
    GRACE_CHECK_EQ(ease(Ease::QuadIn, -0.5f), 0.0f);
    GRACE_CHECK_EQ(ease(Ease::QuadIn, 1.5f), 1.0f);

    // A tween that was never started stays idle: finished() is true, value()
    // reads 0 and update() is a no-op.
    {
        Tween idle;
        GRACE_CHECK(idle.finished());
        GRACE_CHECK(!idle.running());
        GRACE_CHECK_EQ(idle.value(), 0.0f);
        GRACE_CHECK_EQ(idle.progress(), 1.0f);
        idle.update(0.5f);
        GRACE_CHECK(idle.finished());
        GRACE_CHECK_EQ(idle.value(), 0.0f);
    }

    // A running tween: start, advance, read the value, finish.
    {
        Tween tween;
        tween.start(0.0f, 10.0f, 1.0f, Ease::Linear);
        GRACE_CHECK(!tween.finished());
        GRACE_CHECK(tween.running());
        GRACE_CHECK_EQ(tween.value(), 0.0f);
        GRACE_CHECK_EQ(tween.progress(), 0.0f);
        tween.update(0.5f);
        GRACE_CHECK(near(tween.value(), 5.0f, 1e-6f));
        GRACE_CHECK_EQ(tween.progress(), 0.5f);
        tween.update(0.5f);
        GRACE_CHECK(near(tween.value(), 10.0f, 1e-6f));
        GRACE_CHECK_EQ(tween.progress(), 1.0f);
        GRACE_CHECK(tween.finished());
        GRACE_CHECK(!tween.running());
        // Repeated updates after the end are safe and idempotent.
        tween.update(0.5f);
        tween.update(100.0f);
        GRACE_CHECK(near(tween.value(), 10.0f, 1e-6f));
        GRACE_CHECK(tween.finished());
    }

    // The easing shapes the progress: a quad-out tween is ahead of the
    // linear one at the same elapsed fraction.
    {
        Tween linear;
        Tween quadOut;
        linear.start(0.0f, 1.0f, 1.0f, Ease::Linear);
        quadOut.start(0.0f, 1.0f, 1.0f, Ease::QuadOut);
        linear.update(0.25f);
        quadOut.update(0.25f);
        GRACE_CHECK(near(linear.value(), 0.25f, 1e-6f));
        GRACE_CHECK(near(quadOut.value(), 0.4375f, 1e-6f));
    }

    // The elapsed fraction clamps at the duration: one update past the end
    // reports the end value and finished() from then on.
    {
        Tween tween;
        tween.start(2.0f, 4.0f, 1.0f, Ease::QuadIn);
        tween.update(0.75f);
        GRACE_CHECK(!tween.finished());
        tween.update(2.0f);
        GRACE_CHECK(near(tween.value(), 4.0f, 1e-6f));
        GRACE_CHECK(tween.finished());
    }

    // A tween can be restarted: start() resets the elapsed time and the
    // finished flag.
    {
        Tween tween;
        tween.start(0.0f, 1.0f, 1.0f, Ease::Linear);
        tween.update(1.0f);
        GRACE_CHECK(tween.finished());
        tween.start(5.0f, 6.0f, 1.0f, Ease::Linear);
        GRACE_CHECK(!tween.finished());
        GRACE_CHECK_EQ(tween.value(), 5.0f);
        tween.update(0.5f);
        GRACE_CHECK(near(tween.value(), 5.5f, 1e-6f));
    }

    // A zero or negative duration is rejected: the same rejection rule the
    // other subsystems' invalid_argument throws follow.
    {
        Tween tween;
        GRACE_CHECK(throwsInvalidArgument([&] { tween.start(0.0f, 1.0f, 0.0f, Ease::Linear); }));
        GRACE_CHECK(throwsInvalidArgument([&] { tween.start(0.0f, 1.0f, -1.0f, Ease::Linear); }));
        GRACE_CHECK(throwsInvalidArgument([&] { tween.start(0.0f, 1.0f, std::nanf(""), Ease::Linear); }));
        GRACE_CHECK(throwsInvalidArgument([&] { tween.start(0.0f, 1.0f, std::nanf("1"), Ease::Linear); }));
        GRACE_CHECK(tween.finished());
    }

    // A two-component tween: the same duration and easing drive both
    // components, and update() advances both at once.
    {
        Tween2 tween;
        tween.start(vec2{0.0f, 0.0f}, vec2{10.0f, -20.0f}, 1.0f, Ease::Linear);
        GRACE_CHECK(!tween.finished());
        GRACE_CHECK(nearVec(tween.value(), vec2{0.0f, 0.0f}, 1e-6f));
        tween.update(0.5f);
        GRACE_CHECK(nearVec(tween.value(), vec2{5.0f, -10.0f}, 1e-6f));
        GRACE_CHECK_EQ(tween.progress(), 0.5f);
        tween.update(0.5f);
        GRACE_CHECK(nearVec(tween.value(), vec2{10.0f, -20.0f}, 1e-6f));
        GRACE_CHECK_EQ(tween.progress(), 1.0f);
        GRACE_CHECK(tween.finished());
    }

    // A two-component tween that was never started stays idle.
    {
        Tween2 idle;
        GRACE_CHECK(idle.finished());
        GRACE_CHECK(nearVec(idle.value(), vec2{0.0f, 0.0f}, 0.0f));
        idle.update(0.5f);
        GRACE_CHECK(idle.finished());
    }

    // A two-component tween with a zero duration is rejected.
    {
        Tween2 tween;
        GRACE_CHECK(throwsInvalidArgument([&] {
            tween.start(vec2{0.0f, 0.0f}, vec2{1.0f, 1.0f}, 0.0f, Ease::Linear);
        }));
        GRACE_CHECK(tween.finished());
    }

    return grace::test::summary("tween");
}
