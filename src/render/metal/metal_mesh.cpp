#include "metal_mesh.hpp"
namespace grace {
namespace render {
namespace metal {
void MtlMesh::upload(const Mesh& mesh)
{
    mesh.validate();
    m_layout = mesh.layout();
    m_vertices = mesh.vertexData();
    m_indices = mesh.indices();
    m_index_count = static_cast<std::uint32_t>(m_indices.size());
}
void MtlMesh::drawIndexed() const
{
    // The backend records the draw from its own command encoder; a draw
    // on a mesh the backend never gave GPU buffers is the backend's
    // violation to count (the contract's draw() is backend-owned too).
}
void MtlMesh::destroy()
{
    // The backend releases the MTLBuffers it created (device ownership);
    // the CPU-side staging dies with the handle.
    m_vertices.clear();
    m_indices.clear();
    m_vertex_buffer = nullptr;
    m_index_buffer = nullptr;
}
}  // namespace metal
}  // namespace render
}  // namespace grace
