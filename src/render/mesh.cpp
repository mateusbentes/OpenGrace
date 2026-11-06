#include "grace/render/mesh.hpp"

#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace grace::render {

void Mesh::setVertexData(const void* data, std::size_t sizeBytes)
{
    if (data == nullptr && sizeBytes != 0)
        throw std::invalid_argument("mesh: null vertex data with non-zero size");
    m_vertices.resize(sizeBytes);
    if (sizeBytes != 0)
        std::memcpy(m_vertices.data(), data, sizeBytes);
}

void Mesh::validate() const
{
    if (m_layout.stride == 0)
        throw std::invalid_argument("mesh: vertex stride must be positive");
    if (m_layout.attributes.empty())
        throw std::invalid_argument("mesh: vertex layout has no attributes");

    for (std::size_t i = 0; i < m_layout.attributes.size(); ++i)
    {
        const VertexAttribute& attr = m_layout.attributes[i];
        if (attr.components == 0 || attr.components > 4)
            throw std::invalid_argument(
                "mesh: attribute component count must be between 1 and 4");
        const std::uint64_t end =
            static_cast<std::uint64_t>(attr.offset) +
            static_cast<std::uint64_t>(attr.components) * componentSize(attr.type);
        if (end > m_layout.stride)
            throw std::invalid_argument(
                "mesh: attribute offset plus size exceeds the vertex stride");
        for (std::size_t j = 0; j < i; ++j)
        {
            if (m_layout.attributes[j].semantic == attr.semantic)
                throw std::invalid_argument("mesh: duplicate vertex attribute semantic");
        }
    }

    if (m_vertices.empty())
        throw std::invalid_argument("mesh: no vertex data");
    if (m_vertices.size() % m_layout.stride != 0)
        throw std::invalid_argument(
            "mesh: vertex data size is not a multiple of the vertex stride");
    if (m_indices.empty())
        throw std::invalid_argument("mesh: no index data");
    if (m_indices.size() % 3 != 0)
        throw std::invalid_argument(
            "mesh: index count must be a multiple of 3 (triangle list)");
    const std::uint32_t count = vertexCount();
    for (const std::uint32_t index : m_indices)
    {
        if (index >= count)
            throw std::invalid_argument("mesh: index out of vertex range");
    }
}

}  // namespace grace::render
