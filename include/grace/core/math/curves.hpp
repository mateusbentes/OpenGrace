#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "grace/core/math/vec3.hpp"

namespace grace::core {

/// Curve kinds the contract supports (GRACE 2's documented curve set:
/// spline, B-spline and TCB). Every kind is evaluated the same way — an
/// ordered control-point list and a parameter in [0, 1] — so a caller can
/// switch kinds without changing its call sites.
enum class CurveKind : std::uint8_t
{
    /// Catmull-Rom spline: interpolates every control point, C1 continuous.
    /// The classic game-animation curve: passes through the keys.
    Spline,
    /// Uniform cubic B-spline: does not pass through the points, but is
    /// C2 continuous. Smoother than the spline; used for camera paths.
    BSpline,
    /// TCB (Kochanek-Bartels): interpolates every control point with
    /// per-point tension, continuity and bias. The superset of the
    /// spline: tension 0, continuity 0, bias 0 reproduces Catmull-Rom.
    TCB
};

/// One key of a 3D curve: a position and, for the TCB kind, the three
/// per-point shape parameters. The parameters default to zero, which is
/// exactly the Catmull-Rom behaviour, so a TCB curve built from plain keys
/// evaluates identically to the Spline kind.
struct CurveKey
{
    vec3 position{};
    float tension = 0.0f;    ///< [-1, 1]: 1 tightens, -1 rounds
    float continuity = 0.0f; ///< [-1, 1]: sign of the incoming/outgoing derivative mismatch
    float bias = 0.0f;       ///< [-1, 1]: direction of the curve at the point
};

/// A curve: a kind, an ordered key list and the evaluation rules. The
/// evaluation is deterministic: the same keys and parameter produce the
/// same point on every platform and in every thread (no hidden state, no
/// floating-point nondeterminism beyond what the arithmetic itself carries).
///
/// The key list must hold at least two keys. With exactly two, every kind
/// reduces to linear interpolation between them (there are no neighbours to
/// form tangents from, and inventing phantom ones would be a policy the
/// caller did not ask for). With three or more, the kind decides the shape:
class Curve
{
public:
    /// Builds a curve. Throws std::invalid_argument when keys has fewer than
    /// two entries, when a TCB parameter is outside [-1, 1], or when any two
    /// adjacent keys share the same parameter position (a zero-length span
    /// makes the arc-length mapping below undefined).
    Curve(CurveKind kind, std::vector<CurveKey> keys);

    CurveKind kind() const noexcept { return m_kind; }
    const std::vector<CurveKey>& keys() const noexcept { return m_keys; }
    std::uint32_t keyCount() const noexcept { return static_cast<std::uint32_t>(m_keys.size()); }
    std::uint32_t spanCount() const noexcept { return keyCount() - 1u; }

    /// Evaluates the curve at parameter t in [0, 1]. t is the *normalised
    /// span index*: t * spanCount is the global segment coordinate, so a
    /// uniform key spacing evaluates at the keys themselves (t = 0 at key 0,
    /// t = 1 at the last key). Out-of-range values are clamped, not wrapped.
    vec3 evaluate(float t) const noexcept;

    /// Tangent (the first derivative) at the same normalised parameter. The
    /// tangent of a segment is constant inside it for every kind, so the
    /// returned vector is the segment's outgoing tangent scaled by the span
    /// count; hosts use it for camera orientation along a path.
    vec3 tangent(float t) const noexcept;

private:
    /// Evaluates one span [i, i+1] at local u in [0, 1] with the four
    /// control points the kind needs (for the ends, the phantom points the
    /// kinds define below).
    vec3 evaluateSpan(std::uint32_t i, float u) const noexcept;

    CurveKind m_kind;
    std::vector<CurveKey> m_keys;
};

namespace detail {

/// The four control points of the Catmull-Rom basis at u, with the span's
/// endpoints p1, p2 and the neighbours p0, p3.
vec3 catmullRom(const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3, float u) noexcept;

/// The Kochanek-Bartels (TCB) interpolation of one span. The incoming and
/// outgoing tangents at each endpoint come from the point's tension,
/// continuity and bias; the span is then a Hermite blend with those
/// tangents. This is the formulation the published TCB paper states.
vec3 tcbSpan(const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3,
             float u, const CurveKey& key1, const CurveKey& key2) noexcept;

/// The uniform cubic B-spline segment: the four points are the span's own
/// endpoints and their immediate neighbours, and the curve does not pass
/// through the points (it passes through the averaging of adjacent spans).
vec3 bsplineSpan(const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3, float u) noexcept;

}  // namespace detail

}  // namespace grace::core
