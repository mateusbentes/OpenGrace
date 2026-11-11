#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "grace/render/shader.hpp"
#include "grace/render/render_backend.hpp"
#include "metal_shader_library.hpp"
#include "mtl_loader.hpp"
namespace grace {
namespace render {
namespace metal {
/// Maps the project's PixelFormat onto Metal pixel formats (inside the
/// Metal module only, the same scoping rule the OpenGL and the Vulkan
/// backends follow). Rgba16F maps onto RGBA16Float, R8 onto R8Unorm; the
/// depth/stencil pair maps onto the platform's depth format (the pick
/// runs at initialise time; Depth24Unorm_Stencil8 is macOS only,
/// Depth32Float_Stencil8 covers both). Declared here so the mapping
/// tests (which run on every platform) can call it directly.
MTLPixelFormat toMtlFormat(grace::render::PixelFormat format);
/// One Metal shader program: the MSL source strings of its two stages plus
/// the staging CPU-side uniform buffer the setUniform calls write into.
///
/// The backend compiles the MSL 2.0 sources in metal_shader_library.hpp at
/// runtime through MTLLibrary (no build-time compiler exists for MSL; the
/// driver's own compiler runs on the device). This class holds the sources
/// and the shared std140 layout table (the same tables the Vulkan library
/// declares: one layout, two shading languages state the same offsets).
/// setUniform writes through the layout table's name lookup into the
/// staging buffer; the backend uploads the buffer before a draw that uses
/// the program (decision 14: the engine sets uniforms by name, the
/// API-neutral contract never changes).
class MtlShaderProgram final : public ShaderProgram
{
public:
    /// One sampler entry of the program's layout table: the texture
    /// argument index the MSL declares and the entry's element count (the
    /// shadow-map arrays declare 4 or 2 elements, the scalar samplers one).
    struct SamplerEntry
    {
        std::string name;
        std::uint32_t textureIndex;
        std::uint32_t count;
        // The engine's texture-unit rule for the entry's elements (the
        // layout table declares it): element e lands on unit = unitBase
        // + unitStride * e.
        std::uint32_t unitBase;
        std::uint32_t unitStride;
    };
    /// Looks the library entry up for `id` and prepares the std140
    /// staging buffer. Throws std::runtime_error when the id has no
    /// library entry. The MSL compilation itself happens at pipeline
    /// creation (the device's own compiler); the broken pair's failure
    /// surfaces there, the same place the OpenGL backend reports it.
    explicit MtlShaderProgram(ShaderProgramId id);
    ~MtlShaderProgram() override = default;
    MtlShaderProgram(const MtlShaderProgram&) = delete;
    MtlShaderProgram& operator=(const MtlShaderProgram&) = delete;
    void use() override;
    void setUniform(const char* name, float value) override;
    void setUniform(const char* name, int value) override;
    void setUniform(const char* name, const core::vec2& value) override;
    void setUniform(const char* name, const core::vec3& value) override;
    void setUniform(const char* name, const core::vec4& value) override;
    void setUniform(const char* name, const core::mat4& value) override;
    void setUniformSampler(const char* name,
                           std::int32_t textureUnit) override;
    std::int32_t uniformLocation(const char* name) override;
    /// The MSL source of the program's stages. The null source means the
    /// program is the deliberately broken pair's stage (the library still
    /// declares it; the runtime compile is expected to fail).
    const char* vertexSource() const noexcept
    {
        return m_vertex_source;
    }
    const char* fragmentSource() const noexcept
    {
        return m_fragment_source;
    }
    /// The staged uniform bytes (std140, sized by the layout table).
    const std::vector<std::byte>& uniformBytes() const noexcept
    {
        return m_uniform_bytes;
    }
    /// Whether any uniform was written since the last clearStaging().
    bool uniformStaged() const noexcept { return m_uniform_staged; }
    void clearStaging() noexcept { m_uniform_staged = false; }
    /// Sampler texture index of a sampler uniform name; -1 when the name
    /// is not a sampler of this program.
    std::int32_t samplerBinding(const std::string& name) const;
    /// The program's sampler entries with the element counts (the draw's
    /// texture bindings follow these).
    const std::vector<SamplerEntry>& samplerEntries() const noexcept
    {
        return m_sampler_entries;
    }
private:
    /// Writes value at the std140 offset of name (a no-op when the program
    /// has no such member, the same tolerance uniformLocation(-1) has).
    void writeUniform(const char* name, const void* data, std::uint32_t size);
    const char* m_vertex_source = nullptr;
    const char* m_fragment_source = nullptr;
    const ShaderBlockLayout::Member* m_members = nullptr;
    std::uint32_t m_member_count = 0;
    std::map<std::string, std::int32_t> m_sampler_bindings;
    std::vector<SamplerEntry> m_sampler_entries;
    std::vector<std::byte> m_uniform_bytes;
    bool m_uniform_staged = false;
};
}  // namespace metal
}  // namespace render
}  // namespace grace
