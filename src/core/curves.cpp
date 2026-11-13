#include "grace/core/math/curves.hpp"

namespace grace::core {

namespace {

/// Throws when a TCB parameter leaves [-1, 1] or the key list is too short.
void validateKeys(CurveKind kind, const std::vector<CurveKey>& keys)
{
    if (keys.size() < 2)
        throw std::invalid_argument("Curve: at least two keys are required");
    if (kind == CurveKind::TCB)
    {
        for (const CurveKey& key : keys)
        {
            const bool tensionOk = key.tension >= -1.0f && key.tension <= 1.0f;
            const bool continuityOk = key.continuity >= -1.0f && key.continuity <= 1.0f;
            const bool biasOk = key.bias >= -1.0f && key.bias <= 1.0f;
            if (!tensionOk || !continuityOk || !biasOk)
                throw std::invalid_argument(
                    "Curve: TCB parameters must stay within [-1, 1]");
        }
    }
}

/// Clamps t into [0, 1].
float clamp01(float t) noexcept
{
    if (t < 0.0f)
        return 0.0f;
    if (t > 1.0f)
        return 1.0f;
    return t;
}

}  // namespace

Curve::Curve(CurveKind kind, std::vector<CurveKey> keys) : m_kind(kind), m_keys(std::move(keys))
{
    validateKeys(m_kind, m_keys);
}

vec3 Curve::evaluate(float t) const noexcept
{
    const float clamped = clamp01(t);
    const float spanCoordinate = clamped * static_cast<float>(spanCount());
    // The last key itself is only reached at t == 1; everywhere else the
    // coordinate stays inside the last open span.
    std::uint32_t span = static_cast<std::uint32_t>(spanCoordinate);
    if (span >= spanCount())
        span = spanCount() - 1u;
    const float u = spanCoordinate - static_cast<float>(span);
    return evaluateSpan(span, u);
}

vec3 Curve::tangent(float t) const noexcept
{
    const float clamped = clamp01(t);
    const float spanCoordinate = clamped * static_cast<float>(spanCount());
    std::uint32_t span = static_cast<std::uint32_t>(spanCoordinate);
    if (span >= spanCount())
        span = spanCount() - 1u;
    // Every kind's segment is a cubic in u; its derivative at u is a
    // quadratic. The contract keeps the documented simplification: the
    // reported tangent is the segment's chord direction scaled to unit
    // length — the direction the curve moves in through that span — which
    // is what a camera-along-path consumer needs and which every kind
    // defines without per-kind derivative code.
    const vec3 chord = m_keys[span + 1u].position - m_keys[span].position;
    const float length = std::sqrt(dot(chord, chord));
    if (length <= 0.0f)
        return vec3{0.0f, 0.0f, 0.0f};
    return chord * (1.0f / length);
}

vec3 Curve::evaluateSpan(std::uint32_t i, float u) const noexcept
{
    // The four points around the span. The ends use the phantom points the
    // kinds define: Catmull-Rom and TCB double the end point (the tangent
    // there points along the first/last span), the B-spline pads with the
    // end points too (the published uniform B-spline end condition).
    const vec3& p1 = m_keys[i].position;
    const vec3& p2 = m_keys[i + 1u].position;
    const vec3 p0 = i == 0 ? p1 : m_keys[i - 1u].position;
    const vec3 p3 = i + 2u >= keyCount() ? p2 : m_keys[i + 2u].position;

    switch (m_kind)
    {
    case CurveKind::Spline:
        return detail::catmullRom(p0, p1, p2, p3, u);
    case CurveKind::BSpline:
        return detail::bsplineSpan(p0, p1, p2, p3, u);
    case CurveKind::TCB:
        return detail::tcbSpan(p0, p1, p2, p3, u, m_keys[i], m_keys[i + 1u]);
    }
    return p1;
}

namespace detail {

vec3 catmullRom(const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3, float u) noexcept
{
    // The uniform Catmull-Rom basis (0.5 * the matrix form).
    const float u2 = u * u;
    const float u3 = u2 * u;
    return (p1 * 2.0f + (p2 - p0) * u + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2 +
            (p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3) *
           0.5f;
}

vec3 bsplineSpan(const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3, float u) noexcept
{
    // The uniform cubic B-spline basis matrix.
    const float u2 = u * u;
    const float u3 = u2 * u;
    return (p0 * (-u3 + 3.0f * u2 - 3.0f * u + 1.0f) + p1 * (3.0f * u3 - 6.0f * u2 + 4.0f) +
            p2 * (-3.0f * u3 + 3.0f * u2 + 3.0f * u + 1.0f) + p3 * u3) *
           (1.0f / 6.0f);
}

vec3 tcbSpan(const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3, float u,
             const CurveKey& key1, const CurveKey& key2) noexcept
{
    // Outgoing tangent at p1 and incoming tangent at p2, from the
    // Kochanek-Bartels formulation:
    //   d1 = (1-t)(1+c)(1+b)/2 * (p1-p0) + (1-t)(1-c)(1-b)/2 * (p2-p1)
    //   d2 = (1-t)(1-c)(1+b)/2 * (p2-p1) + (1-t)(1+c)(1-b)/2 * (p3-p2)
    const float t1 = key1.tension, c1 = key1.continuity, b1 = key1.bias;
    const float t2 = key2.tension, c2 = key2.continuity, b2 = key2.bias;
    const vec3 d1 = (p1 - p0) * ((1.0f - t1) * (1.0f + c1) * (1.0f + b1) * 0.5f) +
                    (p2 - p1) * ((1.0f - t1) * (1.0f - c1) * (1.0f - b1) * 0.5f);
    const vec3 d2 = (p2 - p1) * ((1.0f - t2) * (1.0f - c2) * (1.0f + b2) * 0.5f) +
                    (p3 - p2) * ((1.0f - t2) * (1.0f + c2) * (1.0f - b2) * 0.5f);
    // Hermite blend with the endpoint tangents.
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h10 = u3 - 2.0f * u2 + u;
    const float h01 = -2.0f * u3 + 3.0f * u2;
    const float h11 = u3 - u2;
    return p1 * h00 + d1 * h10 + p2 * h01 + d2 * h11;
}

}  // namespace detail

}  // namespace grace::core
