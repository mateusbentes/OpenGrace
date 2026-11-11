#include "metal_shader.hpp"
#include <cstring>
#include <stdexcept>
namespace grace {
namespace render {
namespace metal {
namespace {
/// The library's stage entry: the MSL source string plus whether the
/// stage is the deliberately broken pair (the source compiles to nothing
/// on the device; the runtime compile failure is the documented
/// violation signal).
struct StageEntry
{
    const char* name;
    const char* source;
};
constexpr StageEntry kStages[] = {
    {"kTriangleVertexShader", kTriangleVertexShader},
    {"kTriangleFragmentShader", kTriangleFragmentShader},
    {"kLightingVertexShader", kLightingVertexShader},
    {"kLightingFragmentShader", kLightingFragmentShader},
    {"kShadowDepthVertexShader", kShadowDepthVertexShader},
    {"kShadowDepthFragmentShader", kShadowDepthFragmentShader},
    {"kParaboloidDepthVertexShader", kParaboloidDepthVertexShader},
    {"kParaboloidDepthFragmentShader", kParaboloidDepthFragmentShader},
    {"kGBufferVertexShader", kGBufferVertexShader},
    {"kGBufferFragmentShader", kGBufferFragmentShader},
    {"kDeferredLightVertexShader", kDeferredLightVertexShader},
    {"kDeferredLightFragmentShader", kDeferredLightFragmentShader},
    {"kFlatVertexShader", kFlatVertexShader},
    {"kFlatFragmentShader", kFlatFragmentShader},
    {"kBrokenVertexShader", kBrokenVertexShader},
    {"kBrokenFragmentShader", kBrokenFragmentShader},
    {"kPostBrightFragmentShader", kPostBrightFragmentShader},
    {"kPostBlurFragmentShader", kPostBlurFragmentShader},
    {"kPostTonemapFragmentShader", kPostTonemapFragmentShader},
    {"kPostSSAOFragmentShader", kPostSSAOFragmentShader},
    {"kPostSSAOStfFragmentShader", kPostSSAOStfFragmentShader},
    {"kPostMLAAFragmentShader", kPostMLAAFragmentShader},
    {"kPostBlitFragmentShader", kPostBlitFragmentShader},
    {"kPostDOFFragmentShader", kPostDOFFragmentShader},
    {"kPostVolumetricFragmentShader", kPostVolumetricFragmentShader},
    {"kVolumeInjectFragmentShader", kVolumeInjectFragmentShader},
    {"kVolumePropagateFragmentShader", kVolumePropagateFragmentShader},
};
const StageEntry* stageEntry(const char* name)
{
    for (const StageEntry& entry : kStages)
    {
        if (std::strcmp(entry.name, name) == 0)
        {
            return &entry;
        }
    }
    return nullptr;
}
/// The program's library entry: the same table the Vulkan program's
/// constructor resolves (the enum's declaration order in
/// grace/render/shader.hpp), so both backends resolve identically.
struct ProgramEntry
{
    ShaderProgramId id;
    const char* vertex_name;    ///< nullptr: no vertex stage (post passes)
    const char* fragment_name;
};
constexpr ProgramEntry kPrograms[] = {
    {ShaderProgramId::FlatUnlit,
     "kFlatVertexShader", "kFlatFragmentShader"},
    {ShaderProgramId::TestBrokenVertex,
     "kBrokenVertexShader", "kFlatFragmentShader"},
    {ShaderProgramId::TestBrokenFragment,
     "kFlatVertexShader", "kBrokenFragmentShader"},
    {ShaderProgramId::ForwardLighting,
     "kLightingVertexShader", "kLightingFragmentShader"},
    {ShaderProgramId::ShadowDepth,
     "kShadowDepthVertexShader", "kShadowDepthFragmentShader"},
    {ShaderProgramId::ParaboloidDepth,
     "kParaboloidDepthVertexShader", "kParaboloidDepthFragmentShader"},
    {ShaderProgramId::GBuffer,
     "kGBufferVertexShader", "kGBufferFragmentShader"},
    {ShaderProgramId::DeferredLighting,
     "kDeferredLightVertexShader", "kDeferredLightFragmentShader"},
    {ShaderProgramId::PostBright, nullptr, "kPostBrightFragmentShader"},
    {ShaderProgramId::PostBlur, nullptr, "kPostBlurFragmentShader"},
    {ShaderProgramId::PostTonemap, nullptr, "kPostTonemapFragmentShader"},
    {ShaderProgramId::PostSSAO, nullptr, "kPostSSAOFragmentShader"},
    {ShaderProgramId::PostSSAOStf, nullptr, "kPostSSAOStfFragmentShader"},
    {ShaderProgramId::PostMLAA, nullptr, "kPostMLAAFragmentShader"},
    {ShaderProgramId::PostDOF, nullptr, "kPostDOFFragmentShader"},
    {ShaderProgramId::PostBlit, nullptr, "kPostBlitFragmentShader"},
    {ShaderProgramId::PostVolumetric, nullptr,
     "kPostVolumetricFragmentShader"},
    {ShaderProgramId::VolumeInject, nullptr, "kVolumeInjectFragmentShader"},
    {ShaderProgramId::VolumePropagate, nullptr,
     "kVolumePropagateFragmentShader"},
};
}  // namespace
MtlShaderProgram::MtlShaderProgram(ShaderProgramId id)
{
    const ProgramEntry* entry = nullptr;
    for (const ProgramEntry& candidate : kPrograms)
    {
        if (candidate.id == id)
        {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr)
    {
        throw std::runtime_error(
            "MtlShaderProgram: no library entry for this ShaderProgramId");
    }
    if (entry->vertex_name != nullptr)
    {
        const StageEntry* stage = stageEntry(entry->vertex_name);
        if (stage == nullptr)
        {
            throw std::runtime_error(std::string("no MSL source for vertex ")
                                     + entry->vertex_name);
        }
        m_vertex_source = stage->source;
    }
    if (entry->fragment_name != nullptr)
    {
        const StageEntry* stage = stageEntry(entry->fragment_name);
        if (stage == nullptr)
        {
            throw std::runtime_error(
                std::string("no MSL source for fragment ")
                + entry->fragment_name);
        }
        m_fragment_source = stage->source;
    }
    // The layout lookup goes through the shared table's shader name, which
    // is the fragment literal's name when there is one, else the vertex
    // one (the same rule the Vulkan program's constructor applies).
    const char* layout_name =
        entry->fragment_name != nullptr ? entry->fragment_name
                                        : entry->vertex_name;
    for (const ShaderLayoutEntry& layout : kShaderLayouts)
    {
        if (std::strcmp(layout.shader, layout_name) == 0)
        {
            m_members = layout.members;
            m_member_count = layout.memberCount;
            for (std::uint32_t i = 0; i < layout.samplerCount; ++i)
            {
                const ShaderSampler& sampler = layout.samplers[i];
                if (sampler.name != nullptr)
                {
                    m_sampler_bindings.emplace(
                        sampler.name,
                        static_cast<std::int32_t>(sampler.binding));
                    m_sampler_entries.push_back(
                        SamplerEntry{sampler.name, sampler.binding,
                                     sampler.count, sampler.unitBase,
                                     sampler.unitStride});
                }
            }
            break;
        }
    }
    const std::size_t byte_count =
        m_members != nullptr && m_member_count > 0
            ? static_cast<std::size_t>(
                  m_members[m_member_count - 1].offset)
                  + m_members[m_member_count - 1].size
            : 0;
    m_uniform_bytes.assign(byte_count, std::byte{0});
}
void MtlShaderProgram::use() {}
void MtlShaderProgram::writeUniform(const char* name, const void* data,
                                    std::uint32_t size)
{
    if (m_members == nullptr || name == nullptr)
    {
        return;
    }
    for (std::uint32_t i = 0; i < m_member_count; ++i)
    {
        const ShaderBlockLayout::Member& member = m_members[i];
        if (std::strcmp(member.name, name) == 0)
        {
            if (size > member.size)
            {
                return;
            }
            std::memcpy(m_uniform_bytes.data() + member.offset, data, size);
            m_uniform_staged = true;
            return;
        }
    }
    // An unknown name is the same tolerance uniformLocation(-1) has: no
    // write, no error (optimised-out or misspelled is contract-legal).
}
void MtlShaderProgram::setUniform(const char* name, float value)
{
    writeUniform(name, &value, sizeof(value));
}
void MtlShaderProgram::setUniform(const char* name, int value)
{
    writeUniform(name, &value, sizeof(value));
}
void MtlShaderProgram::setUniform(const char* name, const core::vec2& value)
{
    writeUniform(name, &value, sizeof(value));
}
void MtlShaderProgram::setUniform(const char* name, const core::vec3& value)
{
    writeUniform(name, &value, sizeof(value));
}
void MtlShaderProgram::setUniform(const char* name, const core::vec4& value)
{
    writeUniform(name, &value, sizeof(value));
}
void MtlShaderProgram::setUniform(const char* name, const core::mat4& value)
{
    writeUniform(name, &value, sizeof(value));
}
void MtlShaderProgram::setUniformSampler(const char* name,
                                         std::int32_t textureUnit)
{
    // The samplers are texture/sampler argument pairs, not buffer
    // members: the unit lands on the draw's texture bindings the backend
    // records. The staging flag is not set: samplers do not dirty the
    // uniform buffer.
    if (name == nullptr)
    {
        return;
    }
    (void)textureUnit;
}
std::int32_t MtlShaderProgram::uniformLocation(const char* name)
{
    if (m_members == nullptr || name == nullptr)
    {
        return -1;
    }
    for (std::uint32_t i = 0; i < m_member_count; ++i)
    {
        if (std::strcmp(m_members[i].name, name) == 0)
        {
            return static_cast<std::int32_t>(m_members[i].offset);
        }
    }
    return -1;
}
std::int32_t MtlShaderProgram::samplerBinding(
    const std::string& name) const
{
    auto it = m_sampler_bindings.find(name);
    return it != m_sampler_bindings.end() ? it->second : -1;
}
}  // namespace metal
}  // namespace render
}  // namespace grace
