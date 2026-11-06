#include "opengl_mesh.hpp"

#include <cstdint>
#include <stdexcept>

namespace grace::render::opengl {

namespace {

/// Attribute location per semantic, matching the shader convention
/// (position = 0, normal = 1, texcoord = 2, color = 3).
GLuint semanticLocation(VertexSemantic semantic)
{
    switch (semantic)
    {
    case VertexSemantic::Position:
        return 0;
    case VertexSemantic::Normal:
        return 1;
    case VertexSemantic::TexCoord0:
        return 2;
    case VertexSemantic::Color:
        return 3;
    }
    return 0;
}

GLenum glType(ComponentType type)
{
    return type == ComponentType::Float32 ? GL_FLOAT : GL_UNSIGNED_BYTE;
}

}  // namespace

void GlMesh::destroy()
{
    if (m_vao != 0)
    {
        m_gl.glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0)
    {
        m_gl.glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ibo != 0)
    {
        m_gl.glDeleteBuffers(1, &m_ibo);
        m_ibo = 0;
    }
    m_index_count = 0;
    m_uploaded = false;
}

void GlMesh::upload(const Mesh& mesh)
{
    mesh.validate();  // throws std::invalid_argument with a specific message

    // Re-upload discards all previous GPU data: the objects are recreated.
    destroy();

    const VertexLayout& layout = mesh.layout();
    const std::vector<std::byte>& vertices = mesh.vertexData();
    const std::vector<std::uint32_t>& indices = mesh.indices();

    m_gl.glGenVertexArrays(1, &m_vao);
    m_gl.glBindVertexArray(m_vao);

    m_gl.glGenBuffers(1, &m_vbo);
    m_gl.glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl.glBufferData(GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(vertices.size()),
                      vertices.data(), GL_STATIC_DRAW);

    m_gl.glGenBuffers(1, &m_ibo);
    m_gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    m_gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                      indices.data(), GL_STATIC_DRAW);

    for (const VertexAttribute& attr : layout.attributes)
    {
        const GLuint location = semanticLocation(attr.semantic);
        m_gl.glEnableVertexAttribArray(location);
        m_gl.glVertexAttribPointer(location, static_cast<GLint>(attr.components),
                                   glType(attr.type),
                                   attr.normalized ? GL_TRUE : GL_FALSE,
                                   static_cast<GLsizei>(layout.stride),
                                   reinterpret_cast<const GLvoid*>(
                                       static_cast<std::uintptr_t>(attr.offset)));
    }

    m_index_count = static_cast<GLsizei>(indices.size());
    m_uploaded = true;
}

void GlMesh::drawIndexed()
{
    if (!m_uploaded)
        throw std::logic_error("GlMesh::drawIndexed: no mesh uploaded");
    m_gl.glBindVertexArray(m_vao);
    m_gl.glDrawElements(GL_TRIANGLES, m_index_count, GL_UNSIGNED_INT, nullptr);
}

}  // namespace grace::render::opengl
