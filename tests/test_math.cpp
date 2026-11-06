#include "grace/core/math/camera.hpp"
#include "grace/core/math/mat4.hpp"
#include "grace/core/math/transform.hpp"
#include "grace/core/math/vec2.hpp"
#include "grace/core/math/vec3.hpp"
#include "grace/core/math/vec4.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::mat4;
using grace::core::vec2;
using grace::core::vec3;
using grace::core::vec4;

constexpr float kPi = 3.14159265358979323846f;

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

bool near(const vec4& a, const vec4& b, float eps)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps) &&
           near(a.w, b.w, eps);
}

bool near(const mat4& a, const mat4& b, float eps)
{
    for (std::uint32_t c = 0; c < 4; ++c)
        for (std::uint32_t r = 0; r < 4; ++r)
            if (!near(a(c, r), b(c, r), eps))
                return false;
    return true;
}

bool throwsInvalidArgumentOnSingularInverse()
{
    try
    {
        static_cast<void>(grace::core::inverseAffine(grace::core::scale(vec3{0.0f, 1.0f, 1.0f})));
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
    // Component access is constexpr where practical.
    constexpr vec3 kConstVec{1.0f, 2.0f, 3.0f};
    static_assert(kConstVec[1] == 2.0f, "constexpr component access");
    static_assert(grace::core::dot(vec2{1.0f, 2.0f}, vec2{3.0f, 4.0f}) == 11.0f, "constexpr dot");

    // Vector arithmetic, dot, cross, length.
    {
        const vec3 a{1.0f, 2.0f, 3.0f};
        const vec3 b{4.0f, 5.0f, 6.0f};
        GRACE_CHECK((a + b == vec3{5.0f, 7.0f, 9.0f}));
        GRACE_CHECK((a - b == vec3{-3.0f, -3.0f, -3.0f}));
        GRACE_CHECK((-a == vec3{-1.0f, -2.0f, -3.0f}));
        GRACE_CHECK((a * 2.0f == vec3{2.0f, 4.0f, 6.0f}));
        GRACE_CHECK(2.0f * a == a * 2.0f);
        GRACE_CHECK((near(grace::core::dot(a, b), 32.0f, 1e-6f)));
        GRACE_CHECK((grace::core::cross(vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}) ==
                     vec3{0.0f, 0.0f, 1.0f}));
        GRACE_CHECK((near(grace::core::length(vec3{3.0f, 4.0f, 0.0f}), 5.0f, 1e-6f)));
        GRACE_CHECK((near(grace::core::length(grace::core::normalise(vec3{3.0f, 4.0f, 0.0f})),
                         1.0f, 1e-6f)));
    }

    // Degenerate inputs (D4): in Debug builds they are reported through the
    // violation handler (and abort without one); in Release builds the
    // documented behaviour is preserved exactly and silently.
    {
        const vec3 zero = vec3{0.0f, 0.0f, 0.0f};
#ifndef NDEBUG
        // Debug: install a handler and verify each degenerate case is
        // reported with the function name and the invalid input.
        static std::vector<std::pair<std::string, std::string>> reports;
        reports.clear();
        grace::core::math::detail::setViolationHandler(
            [](const char* function, const char* what) {
                reports.emplace_back(function, what);
            });
        const vec3 z = grace::core::normalise(zero);
        static_cast<void>(grace::core::rotation(zero, 1.0f));
        static_cast<void>(grace::core::lookAt(vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, 1.0f},
                                             vec3{0.0f, 0.0f, 1.0f}));
        bool named_normalise = false;
        bool named_rotation = false;
        bool named_lookAt = false;
        for (const auto& report : reports)
        {
            if (report.first == "normalise")
                named_normalise = true;
            if (report.first == "rotation")
                named_rotation = true;
            if (report.first == "lookAt")
                named_lookAt = true;
        }
        GRACE_CHECK(named_normalise);
        GRACE_CHECK(named_rotation);
        GRACE_CHECK(named_lookAt);
        GRACE_CHECK((z == vec3{0.0f, 0.0f, 0.0f}));
        GRACE_CHECK(!std::isnan(z.x) && !std::isnan(z.y) && !std::isnan(z.z));
        grace::core::math::detail::setViolationHandler(nullptr);
#else
        // Release: no handler, silent, documented results preserved exactly.
        const vec3 z = grace::core::normalise(zero);
        static_cast<void>(grace::core::rotation(zero, 1.0f));
        static_cast<void>(grace::core::lookAt(vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, 1.0f},
                                             vec3{0.0f, 0.0f, 1.0f}));
        GRACE_CHECK((z == vec3{0.0f, 0.0f, 0.0f}));
        GRACE_CHECK(!std::isnan(z.x) && !std::isnan(z.y) && !std::isnan(z.z));
#endif
    }

    // mat4 identity and matrix-vector product.
    {
        const mat4 identity = mat4::identity();
        const vec4 v{1.0f, 2.0f, 3.0f, 1.0f};
        GRACE_CHECK(identity * v == v);
    }

    // Matrix multiply is associative (within tolerance).
    {
        const mat4 a = grace::core::translation(vec3{1.0f, 2.0f, 3.0f});
        const mat4 b = grace::core::rotation(vec3{0.0f, 0.0f, 1.0f}, 0.7f);
        const mat4 c = grace::core::scale(vec3{2.0f, 3.0f, 4.0f});
        GRACE_CHECK((near((a * b) * c, a * (b * c), 1e-4f)));
    }

    // Translation and scale matrices move points as expected.
    {
        const mat4 t = grace::core::translation(vec3{1.0f, 2.0f, 3.0f});
        GRACE_CHECK((t * vec4{0.0f, 0.0f, 0.0f, 1.0f} == vec4{1.0f, 2.0f, 3.0f, 1.0f}));
        const mat4 s = grace::core::scale(vec3{2.0f, 2.0f, 2.0f});
        GRACE_CHECK((s * vec4{1.0f, 1.0f, 1.0f, 1.0f} == vec4{2.0f, 2.0f, 2.0f, 1.0f}));
        const mat4 r = grace::core::rotation(vec3{0.0f, 0.0f, 1.0f}, kPi / 2.0f);
        GRACE_CHECK((near(r * vec4{1.0f, 0.0f, 0.0f, 1.0f}, vec4{0.0f, 1.0f, 0.0f, 1.0f}, 1e-5f)));
    }

    // inverse(M) * M is the identity for an affine transform.
    {
        const mat4 m = grace::core::translation(vec3{1.0f, 2.0f, 3.0f}) *
                       grace::core::rotation(vec3{0.0f, 1.0f, 0.0f}, 0.9f) *
                       grace::core::scale(vec3{2.0f, 3.0f, 4.0f});
        const mat4 inv = grace::core::inverseAffine(m);
        GRACE_CHECK((near(inv * m, mat4::identity(), 1e-4f)));
        GRACE_CHECK((near(m * inv, mat4::identity(), 1e-4f)));
        GRACE_CHECK(throwsInvalidArgumentOnSingularInverse());
    }

    // Transform::compose yields a matrix combining position and scale.
    {
        grace::core::Transform t;
        t.position = vec3{1.0f, 2.0f, 3.0f};
        t.scale = vec3{2.0f, 2.0f, 2.0f};
        const mat4 m = t.compose();
        GRACE_CHECK((near(m * vec4{1.0f, 0.0f, 0.0f, 1.0f}, vec4{3.0f, 2.0f, 3.0f, 1.0f}, 1e-5f)));
        GRACE_CHECK((near(m * vec4{0.0f, 0.0f, 0.0f, 1.0f}, vec4{1.0f, 2.0f, 3.0f, 1.0f}, 1e-5f)));
    }

    // lookAt places the target at a known depth on the -Z axis.
    {
        const mat4 view = grace::core::lookAt(vec3{0.0f, 0.0f, 5.0f}, vec3{0.0f, 0.0f, 0.0f},
                                              vec3{0.0f, 1.0f, 0.0f});
        GRACE_CHECK((near(view * vec4{0.0f, 0.0f, 0.0f, 1.0f}, vec4{0.0f, 0.0f, -5.0f, 1.0f}, 1e-5f)));
        GRACE_CHECK((near(view * vec4{0.0f, 0.0f, 5.0f, 1.0f}, vec4{0.0f, 0.0f, 0.0f, 1.0f}, 1e-5f)));
    }

    // Perspective (vertical FOV in radians, OpenGL clip convention) maps known
    // points to known clip coordinates: near -> ndc z -1, far -> +1, and a
    // point (1, 0, -1) with fovY 90 and aspect 1 -> ndc x exactly 1.
    {
        const mat4 p = grace::core::perspective(kPi / 2.0f, 1.0f, 0.1f, 100.0f);
        const vec4 nearClip = p * vec4{0.0f, 0.0f, -0.1f, 1.0f};
        const vec4 farClip = p * vec4{0.0f, 0.0f, -100.0f, 1.0f};
        const vec4 sideClip = p * vec4{1.0f, 0.0f, -1.0f, 1.0f};
        GRACE_CHECK((near(nearClip.z / nearClip.w, -1.0f, 1e-4f)));
        GRACE_CHECK((near(farClip.z / farClip.w, 1.0f, 1e-3f)));
        GRACE_CHECK((near(sideClip.x / sideClip.w, 1.0f, 1e-5f)));
        GRACE_CHECK((near(sideClip.y / sideClip.w, 0.0f, 1e-5f)));

        const mat4 pWide = grace::core::perspective(kPi / 2.0f, 2.0f, 0.1f, 100.0f);
        const vec4 wideClip = pWide * vec4{1.0f, 0.0f, -1.0f, 1.0f};
        GRACE_CHECK((near(wideClip.x / wideClip.w, 0.5f, 1e-5f)));  // f / aspect = 1 / 2
    }

    return grace::test::summary("math");
}
