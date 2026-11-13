#include "grace/render/opengl/gl_loader.hpp"

#include <cstring>

namespace grace::render::opengl {

namespace {

template <typename Fn>
bool loadOne(GlLoader::ResolveFn resolve, const char* name, Fn& out, std::string& missing)
{
    void* symbol = resolve(name);
    if (symbol == nullptr)
    {
        missing = name;
        return false;
    }
    static_assert(sizeof(out) == sizeof(symbol), "function pointer size mismatch");
    std::memcpy(&out, &symbol, sizeof(out));
    return true;
}

}  // namespace

bool GlLoader::load(ResolveFn resolve, std::string& missingSymbol)
{
    missingSymbol.clear();

#define GRACE_GL_LOAD(field, name)                       \
    do                                                   \
    {                                                    \
        if (!loadOne(resolve, name, field, missingSymbol)) \
            return false;                                \
    } while (false)

    GRACE_GL_LOAD(glGetString, "glGetString");
    GRACE_GL_LOAD(glGetError, "glGetError");
    GRACE_GL_LOAD(glGetIntegerv, "glGetIntegerv");
    GRACE_GL_LOAD(glClearColor, "glClearColor");
    GRACE_GL_LOAD(glClearDepth, "glClearDepth");
    GRACE_GL_LOAD(glClearStencil, "glClearStencil");
    GRACE_GL_LOAD(glClear, "glClear");
    GRACE_GL_LOAD(glViewport, "glViewport");
    GRACE_GL_LOAD(glReadPixels, "glReadPixels");
    GRACE_GL_LOAD(glEnable, "glEnable");
    GRACE_GL_LOAD(glDisable, "glDisable");
    GRACE_GL_LOAD(glDepthFunc, "glDepthFunc");
    GRACE_GL_LOAD(glDepthMask, "glDepthMask");

    GRACE_GL_LOAD(glGenVertexArrays, "glGenVertexArrays");
    GRACE_GL_LOAD(glBindVertexArray, "glBindVertexArray");
    GRACE_GL_LOAD(glDeleteVertexArrays, "glDeleteVertexArrays");
    GRACE_GL_LOAD(glGenBuffers, "glGenBuffers");
    GRACE_GL_LOAD(glBindBuffer, "glBindBuffer");
    GRACE_GL_LOAD(glBufferData, "glBufferData");
    GRACE_GL_LOAD(glDeleteBuffers, "glDeleteBuffers");
    GRACE_GL_LOAD(glCreateShader, "glCreateShader");
    GRACE_GL_LOAD(glShaderSource, "glShaderSource");
    GRACE_GL_LOAD(glCompileShader, "glCompileShader");
    GRACE_GL_LOAD(glGetShaderiv, "glGetShaderiv");
    GRACE_GL_LOAD(glGetShaderInfoLog, "glGetShaderInfoLog");
    GRACE_GL_LOAD(glDeleteShader, "glDeleteShader");
    GRACE_GL_LOAD(glCreateProgram, "glCreateProgram");
    GRACE_GL_LOAD(glAttachShader, "glAttachShader");
    GRACE_GL_LOAD(glLinkProgram, "glLinkProgram");
    GRACE_GL_LOAD(glGetProgramiv, "glGetProgramiv");
    GRACE_GL_LOAD(glGetProgramInfoLog, "glGetProgramInfoLog");
    GRACE_GL_LOAD(glUseProgram, "glUseProgram");
    GRACE_GL_LOAD(glDeleteProgram, "glDeleteProgram");
    GRACE_GL_LOAD(glGetUniformLocation, "glGetUniformLocation");
    GRACE_GL_LOAD(glUniform1f, "glUniform1f");
    GRACE_GL_LOAD(glUniform1i, "glUniform1i");
    GRACE_GL_LOAD(glUniform2f, "glUniform2f");
    GRACE_GL_LOAD(glUniform3f, "glUniform3f");
    GRACE_GL_LOAD(glUniform4f, "glUniform4f");
    GRACE_GL_LOAD(glUniformMatrix4fv, "glUniformMatrix4fv");
    GRACE_GL_LOAD(glVertexAttribPointer, "glVertexAttribPointer");
    GRACE_GL_LOAD(glEnableVertexAttribArray, "glEnableVertexAttribArray");
    GRACE_GL_LOAD(glDrawArrays, "glDrawArrays");
    GRACE_GL_LOAD(glDrawElements, "glDrawElements");
    GRACE_GL_LOAD(glGenFramebuffers, "glGenFramebuffers");
    GRACE_GL_LOAD(glBindFramebuffer, "glBindFramebuffer");
    GRACE_GL_LOAD(glBlitFramebuffer, "glBlitFramebuffer");
    GRACE_GL_LOAD(glFramebufferTexture2D, "glFramebufferTexture2D");
    GRACE_GL_LOAD(glFramebufferTextureLayer, "glFramebufferTextureLayer");
    GRACE_GL_LOAD(glCheckFramebufferStatus, "glCheckFramebufferStatus");
    GRACE_GL_LOAD(glDeleteFramebuffers, "glDeleteFramebuffers");
    GRACE_GL_LOAD(glGenTextures, "glGenTextures");
    GRACE_GL_LOAD(glBindTexture, "glBindTexture");
    GRACE_GL_LOAD(glTexImage2D, "glTexImage2D");
    GRACE_GL_LOAD(glTexImage3D, "glTexImage3D");
    GRACE_GL_LOAD(glTexSubImage2D, "glTexSubImage2D");
    GRACE_GL_LOAD(glTexSubImage3D, "glTexSubImage3D");
    GRACE_GL_LOAD(glGetTexImage, "glGetTexImage");
    GRACE_GL_LOAD(glPixelStorei, "glPixelStorei");
    GRACE_GL_LOAD(glTexParameteri, "glTexParameteri");
    GRACE_GL_LOAD(glDeleteTextures, "glDeleteTextures");
    GRACE_GL_LOAD(glActiveTexture, "glActiveTexture");
    GRACE_GL_LOAD(glDrawBuffers, "glDrawBuffers");
    GRACE_GL_LOAD(glDrawBuffer, "glDrawBuffer");
    GRACE_GL_LOAD(glReadBuffer, "glReadBuffer");

#undef GRACE_GL_LOAD

    return true;
}

}  // namespace grace::render::opengl
