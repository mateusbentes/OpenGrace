#pragma once

#include <GL/gl.h>

#include <string>

#include "grace/render/opengl/gl_loader.hpp"
#include "grace/render/shader.hpp"

#include "shader_library.hpp"

namespace grace::render::opengl {

/// OpenGL implementation of ShaderProgram. Compilation and link failures
/// throw std::runtime_error carrying the actual info log text.
class GlShaderProgram final : public ShaderProgram
{
public:
    /// Compiles and links the sources the backend's library holds for `id`.
    GlShaderProgram(GlLoader& gl, ShaderProgramId id);
    ~GlShaderProgram() override { destroy(); }

    GlShaderProgram(const GlShaderProgram&) = delete;
    GlShaderProgram& operator=(const GlShaderProgram&) = delete;

    void use() override;

    void setUniform(const char* name, float value) override;
    void setUniform(const char* name, int value) override;
    void setUniform(const char* name, const core::vec2& value) override;
    void setUniform(const char* name, const core::vec3& value) override;
    void setUniform(const char* name, const core::vec4& value) override;
    void setUniform(const char* name, const core::mat4& value) override;
    void setUniformSampler(const char* name, std::int32_t textureUnit) override;

    std::int32_t uniformLocation(const char* name) override;

private:
    GLuint compileStage(bool vertex, const std::string& source);
    GLuint link(GLuint vertexShader, GLuint fragmentShader);
    void destroy();

    GlLoader& m_gl;
    GLuint m_program = 0;
    UniformCache m_cache;
};

}  // namespace grace::render::opengl
