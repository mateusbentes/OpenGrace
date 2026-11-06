#include "opengl_shader.hpp"

#include <stdexcept>

namespace grace::render::opengl {

GlShaderProgram::GlShaderProgram(GlLoader& gl, ShaderProgramId id)
    : m_gl(gl)
{
    const char* vertexSource = nullptr;
    const char* fragmentSource = nullptr;
    switch (id)
    {
    case ShaderProgramId::FlatUnlit:
        vertexSource = kFlatVertexShader;
        fragmentSource = kFlatFragmentShader;
        break;
    case ShaderProgramId::TestBrokenVertex:
        vertexSource = kBrokenVertexShader;
        fragmentSource = kFlatFragmentShader;
        break;
    case ShaderProgramId::TestBrokenFragment:
        vertexSource = kFlatVertexShader;
        fragmentSource = kBrokenFragmentShader;
        break;
    case ShaderProgramId::ForwardLighting:
        vertexSource = kLightingVertexShader;
        fragmentSource = kLightingFragmentShader;
        break;
    case ShaderProgramId::ShadowDepth:
        vertexSource = kShadowDepthVertexShader;
        fragmentSource = kShadowDepthFragmentShader;
        break;
    case ShaderProgramId::ParaboloidDepth:
        vertexSource = kParaboloidDepthVertexShader;
        fragmentSource = kParaboloidDepthFragmentShader;
        break;
    case ShaderProgramId::GBuffer:
        vertexSource = kGBufferVertexShader;
        fragmentSource = kGBufferFragmentShader;
        break;
    case ShaderProgramId::DeferredLighting:
        vertexSource = kDeferredLightVertexShader;
        fragmentSource = kDeferredLightFragmentShader;
        break;
    }
    if (vertexSource == nullptr || fragmentSource == nullptr)
        throw std::runtime_error("GlShaderProgram: unknown ShaderProgramId");
    const GLuint vertex = compileStage(true, vertexSource);
    const GLuint fragment = compileStage(false, fragmentSource);
    m_program = link(vertex, fragment);
}

void GlShaderProgram::destroy()
{
    if (m_program != 0)
    {
        m_gl.glDeleteProgram(m_program);
        m_program = 0;
    }
}

GLuint GlShaderProgram::compileStage(bool vertex, const std::string& source)
{
    const GLuint shader = m_gl.glCreateShader(vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const char* text = source.c_str();
    m_gl.glShaderSource(shader, 1, &text, nullptr);
    m_gl.glCompileShader(shader);
    GLint status = GL_FALSE;
    m_gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        m_gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        m_gl.glGetShaderInfoLog(shader, length, nullptr, log.data());
        m_gl.glDeleteShader(shader);
        throw std::runtime_error(std::string("shader ") +
                                 (vertex ? "vertex" : "fragment") +
                                 " stage compile failed, info log: " + log);
    }
    return shader;
}

GLuint GlShaderProgram::link(GLuint vertexShader, GLuint fragmentShader)
{
    const GLuint program = m_gl.glCreateProgram();
    m_gl.glAttachShader(program, vertexShader);
    m_gl.glAttachShader(program, fragmentShader);
    m_gl.glLinkProgram(program);
    m_gl.glDeleteShader(vertexShader);
    m_gl.glDeleteShader(fragmentShader);
    GLint status = GL_FALSE;
    m_gl.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        m_gl.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        m_gl.glGetProgramInfoLog(program, length, nullptr, log.data());
        m_gl.glDeleteProgram(program);
        throw std::runtime_error("shader program link failed, info log: " + log);
    }
    return program;
}

void GlShaderProgram::use()
{
    m_gl.glUseProgram(m_program);
}

std::int32_t GlShaderProgram::uniformLocation(const char* name)
{
    return m_cache.location(name, [this](const std::string& n) {
        return static_cast<std::int32_t>(m_gl.glGetUniformLocation(m_program, n.c_str()));
    });
}

void GlShaderProgram::setUniform(const char* name, float value)
{
    const GLint location = uniformLocation(name);
    if (location >= 0)
        m_gl.glUniform1f(location, value);
}

void GlShaderProgram::setUniform(const char* name, int value)
{
    const GLint location = uniformLocation(name);
    if (location >= 0)
        m_gl.glUniform1i(location, value);
}

void GlShaderProgram::setUniform(const char* name, const core::vec2& value)
{
    const GLint location = uniformLocation(name);
    if (location >= 0)
        m_gl.glUniform2f(location, value.x, value.y);
}

void GlShaderProgram::setUniform(const char* name, const core::vec3& value)
{
    const GLint location = uniformLocation(name);
    if (location >= 0)
        m_gl.glUniform3f(location, value.x, value.y, value.z);
}

void GlShaderProgram::setUniform(const char* name, const core::vec4& value)
{
    const GLint location = uniformLocation(name);
    if (location >= 0)
        m_gl.glUniform4f(location, value.x, value.y, value.z, value.w);
}

void GlShaderProgram::setUniform(const char* name, const core::mat4& value)
{
    const GLint location = uniformLocation(name);
    if (location >= 0)
        m_gl.glUniformMatrix4fv(location, 1, GL_FALSE, &value.cols[0][0]);
}

void GlShaderProgram::setUniformSampler(const char* name, std::int32_t textureUnit)
{
    const GLint location = uniformLocation(name);
    if (location >= 0)
        m_gl.glUniform1i(location, textureUnit);
}

}  // namespace grace::render::opengl
