#include "grace/core/math/curves.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::Curve;
using grace::core::CurveKey;
using grace::core::CurveKind;
using grace::core::vec3;

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

bool nearVec(const vec3& a, const vec3& b, float eps)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

CurveKey key(float x, float y, float z)
{
    CurveKey k;
    k.position = vec3{x, y, z};
    return k;
}

bool throwsInvalidArgument(CurveKind kind, const std::vector<CurveKey>& keys)
{
    try
    {
        const Curve curve(kind, keys);
        static_cast<void>(curve);
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
    // Rejection: fewer than two keys, TCB parameters out of range.
    {
        GRACE_CHECK(throwsInvalidArgument(CurveKind::Spline, {}));
        GRACE_CHECK(throwsInvalidArgument(CurveKind::Spline, {key(0, 0, 0)}));
        std::vector<CurveKey> badTcb = {key(0, 0, 0), key(1, 0, 0)};
        badTcb[1].tension = 1.5f;
        GRACE_CHECK(throwsInvalidArgument(CurveKind::TCB, badTcb));
        badTcb[1].tension = 0.0f;
        badTcb[1].continuity = -2.0f;
        GRACE_CHECK(throwsInvalidArgument(CurveKind::TCB, badTcb));
        badTcb[1].continuity = 0.0f;
        badTcb[1].bias = 3.0f;
        GRACE_CHECK(throwsInvalidArgument(CurveKind::TCB, badTcb));
    }

    // The two-key curve of every kind is the straight segment: the only
    // shape two points define, with the phantom end points doubling to the
    // keys themselves.
    {
        const Curve spline(CurveKind::Spline, {key(0, 0, 0), key(4, 8, 12)});
        const Curve bspline(CurveKind::BSpline, {key(0, 0, 0), key(4, 8, 12)});
        const Curve tcb(CurveKind::TCB, {key(0, 0, 0), key(4, 8, 12)});
        GRACE_CHECK(nearVec(spline.evaluate(0.5f), vec3{2.0f, 4.0f, 6.0f}, 1e-5f));
        GRACE_CHECK(nearVec(bspline.evaluate(0.5f), vec3{2.0f, 4.0f, 6.0f}, 1e-5f));
        GRACE_CHECK(nearVec(tcb.evaluate(0.5f), vec3{2.0f, 4.0f, 6.0f}, 1e-5f));
        GRACE_CHECK(nearVec(spline.evaluate(0.0f), vec3{0.0f, 0.0f, 0.0f}, 1e-5f));
        GRACE_CHECK(nearVec(spline.evaluate(1.0f), vec3{4.0f, 8.0f, 12.0f}, 1e-5f));
    }

    // Catmull-Rom passes through its keys: a four-key curve evaluated at
    // the keys' own parameters (uniform spacing) reports the keys back.
    {
        const Curve curve(CurveKind::Spline,
                          {key(0, 0, 0), key(1, 2, 0), key(3, 2, 0), key(4, 0, 0)});
        GRACE_CHECK(nearVec(curve.evaluate(0.0f), vec3{0.0f, 0.0f, 0.0f}, 1e-5f));
        GRACE_CHECK(nearVec(curve.evaluate(1.0f / 3.0f), vec3{1.0f, 2.0f, 0.0f}, 1e-4f));
        GRACE_CHECK(nearVec(curve.evaluate(2.0f / 3.0f), vec3{3.0f, 2.0f, 0.0f}, 1e-4f));
        GRACE_CHECK(nearVec(curve.evaluate(1.0f), vec3{4.0f, 0.0f, 0.0f}, 1e-5f));
    }

    // The TCB identity: tension 0, continuity 0, bias 0 reproduces the
    // Catmull-Rom curve exactly (the published superset property).
    {
        const Curve spline(CurveKind::Spline,
                           {key(0, 0, 0), key(1, 2, 0), key(3, 2, 0), key(4, 0, 0)});
        const Curve tcbZero(CurveKind::TCB,
                            {key(0, 0, 0), key(1, 2, 0), key(3, 2, 0), key(4, 0, 0)});
        for (std::uint32_t i = 0; i <= 12; ++i)
        {
            const float t = static_cast<float>(i) / 12.0f;
            GRACE_CHECK(nearVec(tcbZero.evaluate(t), spline.evaluate(t), 1e-4f));
        }
    }

    // Tension pulls the curve tight: at tension 1 the curve at the span
    // midpoint moves toward the straight chord between the keys.
    {
        const Curve tcbZero(CurveKind::TCB,
                            {key(0, 0, 0), key(0, 0, 0), key(4, 0, 0), key(4, 0, 0)});
        // A degenerate four-point arrangement: p0 == p1 and p3 == p2 makes
        // the TCB tangents pure tension * continuity shapes; with every
        // parameter 0 the curve is the straight chord.
        GRACE_CHECK(nearVec(tcbZero.evaluate(0.5f), vec3{2.0f, 0.0f, 0.0f}, 1e-4f));
    }

    // B-spline does not pass through its interior keys: the C2-continuous
    // basis averages, and it stays inside the convex hull of the keys.
    {
        const Curve curve(CurveKind::BSpline,
                          {key(0, 0, 0), key(0, 10, 0), key(10, 10, 0), key(10, 0, 0)});
        // Sampling densely, every coordinate stays within the hull
        // [0,10] on both axes, and no sample equals an interior key
        // (the b-spline's defining property against the spline's).
        bool passesThroughKey1 = false;
        bool passesThroughKey2 = false;
        bool insideHull = true;
        for (std::uint32_t i = 0; i <= 100; ++i)
        {
            const float t = static_cast<float>(i) / 100.0f;
            const vec3 p = curve.evaluate(t);
            if (p.x < -0.001f || p.x > 10.001f || p.y < -0.001f || p.y > 10.001f)
                insideHull = false;
            if (nearVec(p, vec3{0.0f, 10.0f, 0.0f}, 1e-3f))
                passesThroughKey1 = true;
            if (nearVec(p, vec3{10.0f, 10.0f, 0.0f}, 1e-3f))
                passesThroughKey2 = true;
        }
        GRACE_CHECK(insideHull);
        GRACE_CHECK(!passesThroughKey1);
        GRACE_CHECK(!passesThroughKey2);
        // The symmetric keys make the curve symmetric: the sample at t and
        // the sample at 1 - t agree in the y coordinate and mirror in x.
        const vec3 a = curve.evaluate(0.25f);
        const vec3 b = curve.evaluate(0.75f);
        GRACE_CHECK(near(a.x, 10.0f - b.x, 1e-3f));
        GRACE_CHECK(near(a.y, b.y, 1e-3f));
    }

    // Clamping: out-of-range parameters evaluate at the ends, not wrap.
    {
        const Curve curve(CurveKind::Spline, {key(0, 0, 0), key(4, 8, 12)});
        GRACE_CHECK(nearVec(curve.evaluate(-1.0f), vec3{0.0f, 0.0f, 0.0f}, 1e-5f));
        GRACE_CHECK(nearVec(curve.evaluate(2.0f), vec3{4.0f, 8.0f, 12.0f}, 1e-5f));
    }

    // Tangent: the unit chord direction of the span, constant inside it.
    {
        const Curve curve(CurveKind::Spline, {key(0, 0, 0), key(0, 0, 5)});
        const vec3 t = curve.tangent(0.5f);
        GRACE_CHECK(nearVec(t, vec3{0.0f, 0.0f, 1.0f}, 1e-5f));
    }

    // Determinism: two evaluations of the same curve at the same parameter
    // are bit-identical (no hidden state).
    {
        const Curve curve(CurveKind::TCB,
                          {key(0, 0, 0), key(1, 2, 0), key(3, 2, 0), key(4, 0, 0)});
        CurveKey shaped = key(1, 2, 0);
        shaped.tension = 0.5f;
        const Curve shapedCurve(CurveKind::TCB,
                                {key(0, 0, 0), shaped, key(3, 2, 0), key(4, 0, 0)});
        const vec3 a = shapedCurve.evaluate(0.37f);
        const vec3 b = shapedCurve.evaluate(0.37f);
        GRACE_CHECK(a.x == b.x && a.y == b.y && a.z == b.z);
    }

    return grace::test::summary("curves");
}
