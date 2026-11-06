#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "grace/render/render_backend.hpp"

namespace grace::render {

/// Headless RenderBackend.
///
/// Records every call in order (for example "setClearColor(1,2,3,4)",
/// "clear(1,2,3,4)", "shutdown") and counts protocol violations: any
/// setClearColor/clear/shutdown call after shutdown, including a repeated
/// shutdown. Violating calls are recorded but do not change backend state.
class NullRenderBackend final : public RenderBackend
{
public:
    /// Throws std::invalid_argument when width or height is zero.
    NullRenderBackend(std::uint32_t width, std::uint32_t height);

    std::string describe() const override;
    void setClearColor(ClearColor color) override;
    ClearColor clearColor() const override { return m_clear_color; }
    void clear() override;
    std::uint32_t width() const override { return m_width; }
    std::uint32_t height() const override { return m_height; }
    void shutdown() override;

    /// Recorded call sequence, in order.
    const std::vector<std::string>& events() const noexcept { return m_events; }

    /// Number of protocol violations observed so far.
    std::uint64_t violationCount() const noexcept { return m_violations; }

private:
    void record(std::string event);

    std::uint32_t m_width;
    std::uint32_t m_height;
    ClearColor m_clear_color{0, 0, 0, 255};
    bool m_shutdown = false;
    std::vector<std::string> m_events;
    std::uint64_t m_violations = 0;
};

}  // namespace grace::render
