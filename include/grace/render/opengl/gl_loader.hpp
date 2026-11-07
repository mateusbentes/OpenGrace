#pragma once

// OpenGL types and constants. This header belongs to the OpenGL render module
// only; the platform-neutral contract (grace/render/render_backend.hpp) never
// includes it.
#include <GL/gl.h>

#include <string>

namespace grace::render::opengl {

// Function-pointer signatures for the entry points the engine uses, defined
// locally so the loader depends on no generated or third-party loader header.
using FnGetString = const GLubyte* (*)(GLenum name);
using FnGetError = GLenum (*)();
using FnGetIntegerv = void (*)(GLenum pname, GLint* data);
using FnClearColor = void (*)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
using FnClearDepth = void (*)(GLdouble depth);
using FnClearStencil = void (*)(GLint s);
using FnClear = void (*)(GLbitfield mask);
using FnViewport = void (*)(GLint x, GLint y, GLsizei width, GLsizei height);
using FnReadPixels = void (*)(GLint x, GLint y, GLsizei width, GLsizei height,
                              GLenum format, GLenum type, GLvoid* data);
using FnEnable = void (*)(GLenum cap);
using FnDisable = void (*)(GLenum cap);
using FnDepthFunc = void (*)(GLenum func);
using FnDepthMask = void (*)(GLboolean flag);
using FnGenVertexArrays = void (*)(GLsizei n, GLuint* arrays);
using FnBindVertexArray = void (*)(GLuint array);
using FnDeleteVertexArrays = void (*)(GLsizei n, const GLuint* arrays);
using FnGenBuffers = void (*)(GLsizei n, GLuint* buffers);
using FnBindBuffer = void (*)(GLenum target, GLuint buffer);
using FnBufferData = void (*)(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage);
using FnDeleteBuffers = void (*)(GLsizei n, const GLuint* buffers);
using FnCreateShader = GLuint (*)(GLenum type);
using FnShaderSource = void (*)(GLuint shader, GLsizei count, const GLchar* const* string,
                                const GLint* length);
using FnCompileShader = void (*)(GLuint shader);
using FnGetShaderiv = void (*)(GLuint shader, GLenum pname, GLint* params);
using FnGetShaderInfoLog = void (*)(GLuint shader, GLsizei bufSize, GLsizei* length,
                                    GLchar* infoLog);
using FnDeleteShader = void (*)(GLuint shader);
using FnCreateProgram = GLuint (*)();
using FnAttachShader = void (*)(GLuint program, GLuint shader);
using FnLinkProgram = void (*)(GLuint program);
using FnGetProgramiv = void (*)(GLuint program, GLenum pname, GLint* params);
using FnGetProgramInfoLog = void (*)(GLuint program, GLsizei bufSize, GLsizei* length,
                                     GLchar* infoLog);
using FnUseProgram = void (*)(GLuint program);
using FnDeleteProgram = void (*)(GLuint program);
using FnGetUniformLocation = GLint (*)(GLuint program, const GLchar* name);
using FnUniform1f = void (*)(GLint location, GLfloat v0);
using FnUniform1i = void (*)(GLint location, GLint v0);
using FnUniform2f = void (*)(GLint location, GLfloat v0, GLfloat v1);
using FnUniform3f = void (*)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
using FnUniform4f = void (*)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
using FnUniformMatrix4fv = void (*)(GLint location, GLsizei count, GLboolean transpose,
                                    const GLfloat* value);
using FnVertexAttribPointer = void (*)(GLuint index, GLint size, GLenum type,
                                       GLboolean normalized, GLsizei stride,
                                       const GLvoid* pointer);
using FnEnableVertexAttribArray = void (*)(GLuint index);
using FnDrawArrays = void (*)(GLenum mode, GLint first, GLsizei count);
using FnDrawElements = void (*)(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);
using FnGenFramebuffers = void (*)(GLsizei n, GLuint* ids);
using FnBlitFramebuffer = void (*)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
using FnBindFramebuffer = void (*)(GLenum target, GLuint framebuffer);
using FnFramebufferTexture2D = void (*)(GLenum target, GLenum attachment, GLenum textarget,
                                        GLuint texture, GLint level);
using FnCheckFramebufferStatus = GLenum (*)(GLenum target);
using FnDeleteFramebuffers = void (*)(GLsizei n, const GLuint* framebuffers);
using FnGenTextures = void (*)(GLsizei n, GLuint* textures);
using FnBindTexture = void (*)(GLenum target, GLuint texture);
using FnTexImage2D = void (*)(GLenum target, GLint level, GLint internalformat, GLsizei width,
                              GLsizei height, GLint border, GLenum format, GLenum type,
                              const GLvoid* pixels);
// 3D texture entry points (milestone 7, first increment). glGetTexImage is
// OpenGL 1.0 but is not declared by the system gl.h this project builds
// against, so it is resolved here like the rest rather than linked.
using FnTexImage3D = void (*)(GLenum target, GLint level, GLint internalformat, GLsizei width,
                              GLsizei height, GLsizei depth, GLint border, GLenum format,
                              GLenum type, const GLvoid* pixels);
using FnTexSubImage3D = void (*)(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                 GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                                 GLenum format, GLenum type, const GLvoid* pixels);
using FnGetTexImage = void (*)(GLenum target, GLint level, GLenum format, GLenum type,
                               GLvoid* pixels);
using FnPixelStorei = void (*)(GLenum pname, GLint param);
using FnTexParameteri = void (*)(GLenum target, GLenum pname, GLint param);
using FnDeleteTextures = void (*)(GLsizei n, const GLuint* textures);
using FnActiveTexture = void (*)(GLenum texture);
using FnDrawBuffers = void (*)(GLsizei n, const GLenum* bufs);
using FnDrawBuffer = void (*)(GLenum buf);
using FnReadBuffer = void (*)(GLenum buf);

/// Minimal runtime loader for the OpenGL entry points the engine uses.
///
/// The backend resolves symbols through SDL_GL_GetProcAddress (passed in as a
/// plain function pointer, so this header needs no SDL) and every pointer is
/// checked at load time: the first symbol that cannot be resolved is reported
/// by name. No generated loader, no glad/GLEW dependency.
class GlLoader
{
public:
    /// Matches SDL_GL_GetProcAddress on Linux x86_64 (SDLCALL is empty there).
    using ResolveFn = void* (*)(const char*);

    /// Resolves every entry point used by the engine. Returns false and sets
    /// missingSymbol to the first name that could not be resolved.
    bool load(ResolveFn resolve, std::string& missingSymbol);

    // OpenGL 1.1 entry points.
    FnGetString glGetString = nullptr;
    FnGetError glGetError = nullptr;
    FnGetIntegerv glGetIntegerv = nullptr;
    FnClearColor glClearColor = nullptr;
    FnClearDepth glClearDepth = nullptr;
    FnClearStencil glClearStencil = nullptr;
    FnClear glClear = nullptr;
    FnViewport glViewport = nullptr;
    FnReadPixels glReadPixels = nullptr;
    FnEnable glEnable = nullptr;
    FnDisable glDisable = nullptr;
    FnDepthFunc glDepthFunc = nullptr;
    FnDepthMask glDepthMask = nullptr;

    // OpenGL 3.x entry points (VAO/VBO/shader path).
    FnGenVertexArrays glGenVertexArrays = nullptr;
    FnBindVertexArray glBindVertexArray = nullptr;
    FnDeleteVertexArrays glDeleteVertexArrays = nullptr;
    FnGenBuffers glGenBuffers = nullptr;
    FnBindBuffer glBindBuffer = nullptr;
    FnBufferData glBufferData = nullptr;
    FnDeleteBuffers glDeleteBuffers = nullptr;
    FnCreateShader glCreateShader = nullptr;
    FnShaderSource glShaderSource = nullptr;
    FnCompileShader glCompileShader = nullptr;
    FnGetShaderiv glGetShaderiv = nullptr;
    FnGetShaderInfoLog glGetShaderInfoLog = nullptr;
    FnDeleteShader glDeleteShader = nullptr;
    FnCreateProgram glCreateProgram = nullptr;
    FnAttachShader glAttachShader = nullptr;
    FnLinkProgram glLinkProgram = nullptr;
    FnGetProgramiv glGetProgramiv = nullptr;
    FnGetProgramInfoLog glGetProgramInfoLog = nullptr;
    FnUseProgram glUseProgram = nullptr;
    FnDeleteProgram glDeleteProgram = nullptr;
    FnGetUniformLocation glGetUniformLocation = nullptr;
    FnUniform1f glUniform1f = nullptr;
    FnUniform1i glUniform1i = nullptr;
    FnUniform2f glUniform2f = nullptr;
    FnUniform3f glUniform3f = nullptr;
    FnUniform4f glUniform4f = nullptr;
    FnUniformMatrix4fv glUniformMatrix4fv = nullptr;
    FnVertexAttribPointer glVertexAttribPointer = nullptr;
    FnEnableVertexAttribArray glEnableVertexAttribArray = nullptr;
    FnDrawArrays glDrawArrays = nullptr;
    FnDrawElements glDrawElements = nullptr;
    FnGenFramebuffers glGenFramebuffers = nullptr;
    FnBindFramebuffer glBindFramebuffer = nullptr;
    FnBlitFramebuffer glBlitFramebuffer = nullptr;
    FnFramebufferTexture2D glFramebufferTexture2D = nullptr;
    FnCheckFramebufferStatus glCheckFramebufferStatus = nullptr;
    FnDeleteFramebuffers glDeleteFramebuffers = nullptr;
    FnGenTextures glGenTextures = nullptr;
    FnBindTexture glBindTexture = nullptr;
    FnTexImage2D glTexImage2D = nullptr;
    FnTexImage3D glTexImage3D = nullptr;
    FnTexSubImage3D glTexSubImage3D = nullptr;
    FnGetTexImage glGetTexImage = nullptr;
    FnPixelStorei glPixelStorei = nullptr;
    FnTexParameteri glTexParameteri = nullptr;
    FnDeleteTextures glDeleteTextures = nullptr;
    FnActiveTexture glActiveTexture = nullptr;
    FnDrawBuffers glDrawBuffers = nullptr;
    FnDrawBuffer glDrawBuffer = nullptr;
    FnReadBuffer glReadBuffer = nullptr;
};

}  // namespace grace::render::opengl
