#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include "grace/render/shader.hpp"

namespace grace::render {

/// A typed uniform value. Samplers are set separately via setSampler().
using UniformValue =
    std::variant<float, int, core::vec2, core::vec3, core::vec4, core::mat4>;

/// Surface parameters for a forward lighting pass. Conventions (documented):
/// all colours are linear RGB, not sRGB. Ranges: albedo and specular are
/// reflectances with each component in [0, 1]; shininess is a Blinn-Phong
/// exponent >= 0; emissive is linear radiance with components >= 0 (unbounded
/// above, typically small). Out-of-range values are rejected by
/// validateSurfaceParams() with a clear message, never silently clamped.
struct SurfaceParams
{
    core::vec3 albedo{1.0f, 1.0f, 1.0f};    ///< diffuse reflectance, linear, [0, 1] per component
    core::vec3 specular{0.0f, 0.0f, 0.0f};  ///< specular reflectance, linear, [0, 1]
    float shininess = 32.0f;                ///< Blinn-Phong exponent, >= 0
    core::vec3 emissive{0.0f, 0.0f, 0.0f};  ///< linear emissive radiance, >= 0 per component
};

/// Throws std::invalid_argument with a specific message when out of range.
void validateSurfaceParams(const SurfaceParams& surface);

/// A shader plus its parameter set. This milestone builds the vocabulary
/// only: no resource pipeline, no file format, no caching layer.
class Material
{
public:
    Material() = default;
    explicit Material(std::shared_ptr<ShaderProgram> shader) : m_shader(std::move(shader)) {}

    void setShader(std::shared_ptr<ShaderProgram> shader) noexcept { m_shader = std::move(shader); }
    ShaderProgram* shader() const noexcept { return m_shader.get(); }

    void setParameter(const std::string& name, UniformValue value)
    {
        m_parameters[name] = std::move(value);
    }

    /// Sampler binding: the named sampler uniform is set to textureUnit.
    void setSampler(const std::string& name, std::int32_t textureUnit)
    {
        m_samplers[name] = textureUnit;
    }

    std::size_t parameterCount() const noexcept { return m_parameters.size(); }

    /// Sets the surface parameters (validated; throws std::invalid_argument
    /// on out-of-range values).
    void setSurfaceParams(const SurfaceParams& surface);
    const SurfaceParams& surfaceParams() const noexcept { return m_surface; }

    /// Activates the shader and pushes every parameter to it. Throws
    /// std::logic_error when no shader is set.
    void apply() const
    {
        if (!m_shader)
            throw std::logic_error("material: no shader set");
        m_shader->use();
        for (const auto& entry : m_parameters)
            std::visit([&](const auto& value) { applyValue(*m_shader, entry.first, value); },
                       entry.second);
        for (const auto& entry : m_samplers)
            m_shader->setUniformSampler(entry.first.c_str(), entry.second);
    }

private:
    static void applyValue(ShaderProgram& shader, const std::string& name, float value)
    {
        shader.setUniform(name.c_str(), value);
    }
    static void applyValue(ShaderProgram& shader, const std::string& name, int value)
    {
        shader.setUniform(name.c_str(), value);
    }
    static void applyValue(ShaderProgram& shader, const std::string& name, const core::vec2& value)
    {
        shader.setUniform(name.c_str(), value);
    }
    static void applyValue(ShaderProgram& shader, const std::string& name, const core::vec3& value)
    {
        shader.setUniform(name.c_str(), value);
    }
    static void applyValue(ShaderProgram& shader, const std::string& name, const core::vec4& value)
    {
        shader.setUniform(name.c_str(), value);
    }
    static void applyValue(ShaderProgram& shader, const std::string& name, const core::mat4& value)
    {
        shader.setUniform(name.c_str(), value);
    }

    std::shared_ptr<ShaderProgram> m_shader;
    std::map<std::string, UniformValue> m_parameters;
    std::map<std::string, std::int32_t> m_samplers;
    SurfaceParams m_surface;
};

}  // namespace grace::render
