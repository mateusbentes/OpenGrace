#include "vulkan_mesh.hpp"

namespace grace::render::vulkan {

void VkMesh::upload(const Mesh& mesh)
{
    mesh.validate();
    m_layout = mesh.layout();
    m_vertices = mesh.vertexData();
    m_indices = mesh.indices();
    m_index_count = static_cast<std::uint32_t>(m_indices.size());
}

void VkMesh::drawIndexed() const
{
    // The backend records vkCmdDrawIndexed from its own command buffer; a
    // draw on a mesh the backend never gave GPU buffers is the backend's
    // violation to count (the contract's draw() is backend-owned too).
}

void VkMesh::destroy()
{
    // The backend destroys the VkBuffers it created (device ownership); the
    // CPU-side staging dies with the handle.
    m_vertices.clear();
    m_indices.clear();
    m_vertex_buffer = nullptr;
    m_index_buffer = nullptr;
}

}  // namespace grace::render::vulkan
