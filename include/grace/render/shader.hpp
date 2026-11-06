#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "grace/core/math/mat4.hpp"
#include "grace/core/math/vec2.hpp"
#include "grace/core/math/vec3.hpp"
#include "grace/core/math/vec4.hpp"

namespace grace::render {

/// Vertex and fragment GLSL sources of a shader program.
struct ShaderSources
{
    std::string vertex;
    std::string fragment;
};

/// Throws std::invalid_argument with a clear message when a source is empty.
void validateShaderSources(const ShaderSources& sources);

/// Caches uniform locations by name so repeated sets do not re-query the
/// driver. API-neutral; concrete backends feed their query function in.
class UniformCache
{
public:
    using Query = std::function<std::int32_t(const std::string& name)>;

    /// Returns the location of name, querying through query on first use.
    std::int32_t location(const std::string& name, const Query& query);

    bool contains(const std::string& name) const;
    std::size_t size() const noexcept { return m_locations.size(); }

private:
    std::map<std::string, std::int32_t> m_locations;
};

/// API-neutral shader program: a linked vertex+fragment stage pair with
/// cached uniform locations. No graphics-API types appear here; the concrete
/// implementation lives behind the backend boundary (src/render/opengl/).
///
/// Every failure during compilation or linking must surface the actual
/// compiler/linker info log in the exception message, never a bare status
/// code.
class ShaderProgram
{
public:
    virtual ~ShaderProgram() = default;

    /// Makes this program the active one.
    virtual void use() = 0;

    /// Uniform setters require the program to be active (use() called first),
    /// matching the underlying graphics-API semantics. draw() in a backend
    /// activates the program itself.
    virtual void setUniform(const char* name, float value) = 0;
    virtual void setUniform(const char* name, int value) = 0;
    virtual void setUniform(const char* name, const core::vec2& value) = 0;
    virtual void setUniform(const char* name, const core::vec3& value) = 0;
    virtual void setUniform(const char* name, const core::vec4& value) = 0;
    virtual void setUniform(const char* name, const core::mat4& value) = 0;

    /// Binds a sampler uniform to a texture unit.
    virtual void setUniformSampler(const char* name, std::int32_t textureUnit) = 0;

    /// Cached uniform location; -1 when the uniform does not exist in the
    /// program (optimised out or misspelled).
    virtual std::int32_t uniformLocation(const char* name) = 0;
};

}  // namespace grace::render
