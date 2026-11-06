#include "grace/render/shader.hpp"

#include <stdexcept>

namespace grace::render {

void validateShaderSources(const ShaderSources& sources)
{
    if (sources.vertex.empty())
        throw std::invalid_argument("shader: vertex source is empty");
    if (sources.fragment.empty())
        throw std::invalid_argument("shader: fragment source is empty");
}

std::int32_t UniformCache::location(const std::string& name, const Query& query)
{
    const auto it = m_locations.find(name);
    if (it != m_locations.end())
        return it->second;
    const std::int32_t loc = query(name);
    m_locations.emplace(name, loc);
    return loc;
}

bool UniformCache::contains(const std::string& name) const
{
    return m_locations.find(name) != m_locations.end();
}

}  // namespace grace::render
