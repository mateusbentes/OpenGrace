#pragma once

#include <GL/gl.h>

#include "grace/render/mesh.hpp"
#include "grace/render/opengl/gl_loader.hpp"

namespace grace::render::opengl {

/// OpenGL implementation of GpuMesh: VAO + VBO + IBO. Upload is explicit and
/// replaces all previously uploaded data (the GL objects are recreated).
class GlMesh final : public GpuMesh
{
public:
    explicit GlMesh(GlLoader& gl) : m_gl(gl) {}
    ~GlMesh() override { destroy(); }

    GlMesh(const GlMesh&) = delete;
    GlMesh& operator=(const GlMesh&) = delete;

    void upload(const Mesh& mesh) override;
    void drawIndexed() const override;

private:
    void destroy();

    GlLoader& m_gl;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ibo = 0;
    GLsizei m_index_count = 0;
    bool m_uploaded = false;
};

}  // namespace grace::render::opengl
