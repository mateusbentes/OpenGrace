#pragma once

#include <cstdint>

#include "grace/render/render_backend.hpp"
#include "test_support.hpp"

namespace grace::test {

/// Shared RenderBackend contract checks, run unchanged against every backend
/// implementation (null, OpenGL). Every OpenGrace backend exposes
/// violationCount(). Sequence: setClearColor, frames x clear, shutdown, then
/// one post-shutdown clear, which every backend must count as a violation.
///
/// This header is platform-neutral: it includes no OpenGL or SDL headers, so
/// the null backend stays free of any graphics dependency.
template <typename Backend>
void runContractChecks(Backend& backend, grace::render::ClearColor color,
                       std::uint32_t frames)
{
    GRACE_CHECK(backend.width() > 0u);
    GRACE_CHECK(backend.height() > 0u);
    GRACE_CHECK_EQ(backend.violationCount(), 0u);

    backend.setClearColor(color);
    GRACE_CHECK(backend.clearColor() == color);

    for (std::uint32_t frame = 0; frame < frames; ++frame)
        backend.clear();

    GRACE_CHECK_EQ(backend.violationCount(), 0u);

    backend.shutdown();

    // One call after shutdown: a protocol violation on every backend.
    backend.clear();
    GRACE_CHECK_EQ(backend.violationCount(), 1u);

    GRACE_CHECK(!backend.describe().empty());
}

}  // namespace grace::test
