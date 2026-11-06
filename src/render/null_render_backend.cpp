#include "grace/render/null_render_backend.hpp"

#include <cstdio>
#include <stdexcept>

namespace grace::render {

namespace {

std::string colorText(ClearColor color)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%d,%d,%d,%d", static_cast<int>(color.r),
                  static_cast<int>(color.g), static_cast<int>(color.b),
                  static_cast<int>(color.a));
    return std::string(buffer);
}

}  // namespace

NullRenderBackend::NullRenderBackend(std::uint32_t width, std::uint32_t height)
    : m_width(width), m_height(height)
{
    if (width == 0 || height == 0)
        throw std::invalid_argument("NullRenderBackend: width and height must be positive");
}

std::string NullRenderBackend::describe() const
{
    return "null (headless)";
}

void NullRenderBackend::setClearColor(ClearColor color)
{
    record("setClearColor(" + colorText(color) + ")");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_clear_color = color;
}

void NullRenderBackend::clear()
{
    record("clear(" + colorText(m_clear_color) + ")");
    if (m_shutdown)
        ++m_violations;
}

void NullRenderBackend::shutdown()
{
    record("shutdown");
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_shutdown = true;
}

void NullRenderBackend::record(std::string event)
{
    m_events.push_back(std::move(event));
}

}  // namespace grace::render
