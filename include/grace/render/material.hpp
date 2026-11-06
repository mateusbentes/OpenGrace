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
};

}  // namespace grace::render
