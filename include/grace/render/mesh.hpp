#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace grace::render {

/// Vertex attribute semantics understood by the render module.
enum class VertexSemantic : std::uint8_t
{
    Position,
    Normal,
    TexCoord0,
    Color
};

/// Component type of a vertex attribute.
enum class ComponentType : std::uint8_t
{
    Float32,         ///< 4 bytes per component, floating point
    Uint8Normalized  ///< 1 byte per component, normalised to [0, 1] on upload
};

/// Size of one component of the given type, in bytes.
constexpr std::uint32_t componentSize(ComponentType type) noexcept
{
    return type == ComponentType::Float32 ? 4u : 1u;
}

/// One vertex attribute inside a vertex.
struct VertexAttribute
{
    VertexSemantic semantic;
    ComponentType type;
    std::uint32_t components;  ///< e.g. 3 for a vec3 position
    std::uint32_t offset;      ///< byte offset from the start of a vertex
    bool normalized;           ///< normalise integer components on upload
};

/// Full vertex layout: a stride plus the attributes inside each vertex.
struct VertexLayout
{
    std::uint32_t stride = 0;  ///< bytes per vertex, must be > 0
    std::vector<VertexAttribute> attributes;
};

/// API-neutral indexed mesh: raw vertex bytes described by a layout, plus an
/// index buffer. No graphics-API types appear here; the GPU-side
/// implementation lives behind the backend boundary (src/render/opengl/).
class Mesh
{
public:
    void setLayout(const VertexLayout& layout) { m_layout = layout; }

    /// Copies raw vertex bytes. The size must be a multiple of the stride.
    void setVertexData(const void* data, std::size_t sizeBytes);

    /// Convenience overload for trivially copyable vertex structs.
    template <typename T>
    void setVertexData(const std::vector<T>& values)
    {
        static_assert(std::is_trivially_copyable<T>::value, "vertex type must be trivially copyable");
        setVertexData(values.data(), values.size() * sizeof(T));
    }

    void setIndices(const std::vector<std::uint32_t>& indices) { m_indices = indices; }

    const VertexLayout& layout() const noexcept { return m_layout; }
    const std::vector<std::byte>& vertexData() const noexcept { return m_vertices; }
    const std::vector<std::uint32_t>& indices() const noexcept { return m_indices; }

    std::uint32_t vertexCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_vertices.size() / m_layout.stride);
    }

    /// Throws std::invalid_argument with a specific message when the layout or
    /// the data is invalid (stride, attribute bounds, duplicate semantics,
    /// vertex size mismatch, index range, triangle-list index count).
    void validate() const;

private:
    VertexLayout m_layout;
    std::vector<std::byte> m_vertices;
    std::vector<std::uint32_t> m_indices;
};

/// GPU-side mesh handle, implemented by a concrete backend. Upload is
/// explicit: nothing reaches the GPU until upload() is called. Re-uploading
/// the same handle discards all previously uploaded data and replaces it with
/// the new mesh (buffers are recreated, old contents are gone).
class GpuMesh
{
public:
    virtual ~GpuMesh() = default;

    /// Validates and uploads the mesh. Throws std::invalid_argument when the
    /// mesh is invalid. On re-upload the previous GPU data is replaced.
    virtual void upload(const Mesh& mesh) = 0;

    /// Indexed triangle draw of the last uploaded mesh.
    virtual void drawIndexed() = 0;
};

}  // namespace grace::render
