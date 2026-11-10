#pragma once

#include <cstdint>
#include <vector>

#include "grace/render/mesh.hpp"
#include "vk_loader.hpp"

namespace grace::render::vulkan {

/// Vulkan implementation of GpuMesh. The mesh holds the vertex/index buffer
/// handles plus the CPU-side data the backend uploads; the backend owns the
/// device, the queue and the staging machinery, so the actual VkBuffer
/// creation and destruction go through it (the backend's createGpuBuffers
/// and destroyGpuBuffers).
/// This mirrors how the contract splits ownership: GpuMesh is the engine-side
/// handle, the backend is the device-side owner.
///
/// The attribute layout follows the contract's VertexLayout exactly: the
/// backend builds the VkVertexInputBindingDescription /
/// VkVertexInputAttributeDescription set from it at pipeline creation time
/// (position=0, normal=1, texcoord=2, color=3 by semantic, the same order the
/// OpenGL backend binds).
class VkMesh final : public GpuMesh
{
public:
    VkMesh() = default;
    ~VkMesh() override { destroy(); }

    VkMesh(const VkMesh&) = delete;
    VkMesh& operator=(const VkMesh&) = delete;

    /// Validates the mesh and stages the CPU-side data. The GPU-side upload
    /// happens when the backend records the copy (device-owned); this call is
    /// safe before the backend is initialised only in the sense that it does
    /// not touch the device — the backend rejects the draw instead.
    void upload(const Mesh& mesh) override;

    /// Indexed triangle draw of the last uploaded mesh. The backend records
    /// the actual vkCmdDrawIndexed; a draw before any upload is a protocol
    /// violation the backend counts (the same rule the OpenGL backend uses).
    void drawIndexed() const override;

    const std::vector<std::byte>& vertexData() const noexcept
    {
        return m_vertices;
    }
    const std::vector<std::uint32_t>& indices() const noexcept
    {
        return m_indices;
    }
    const VertexLayout& layout() const noexcept { return m_layout; }
    std::uint32_t indexCount() const noexcept
    {
        return m_index_count;
    }
    std::size_t vertexBytes() const noexcept { return m_vertices.size(); }

    /// The backend-created buffers, bound at draw time. Null until the
    /// backend's createGpuBuffers ran for this mesh.
    VkBuffer vertexBuffer() const noexcept { return m_vertex_buffer; }
    VkBuffer indexBuffer() const noexcept { return m_index_buffer; }
    void setGpuBuffers(VkBuffer vertices, VkBuffer indices)
    {
        m_vertex_buffer = vertices;
        m_index_buffer = indices;
    }
    /// Releases the CPU-side copy after the backend's upload consumed it.
    void clearCpuData()
    {
        m_vertices.clear();
        m_vertices.shrink_to_fit();
        m_indices.clear();
        m_indices.shrink_to_fit();
    }

private:
    void destroy();

    std::vector<std::byte> m_vertices;
    std::vector<std::uint32_t> m_indices;
    // The upload's index count, kept past clearCpuData: the draw's
    // vkCmdDrawIndexed needs it after the backend consumed the CPU copy.
    std::uint32_t m_index_count = 0;
    VertexLayout m_layout;
    VkBuffer m_vertex_buffer = nullptr;
    VkBuffer m_index_buffer = nullptr;
};

}  // namespace grace::render::vulkan
