#include "gl_loader.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

void (REIM_GL_API* glClear)(GLbitfield) = nullptr;
GLenum (REIM_GL_API* glGetError)(void) = nullptr;
void (REIM_GL_API* glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
void (REIM_GL_API* glViewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
void (REIM_GL_API* glEnable)(GLenum) = nullptr;
void (REIM_GL_API* glDisable)(GLenum) = nullptr;
void (REIM_GL_API* glDepthFunc)(GLenum) = nullptr;
void (REIM_GL_API* glDepthMask)(GLboolean) = nullptr;
void (REIM_GL_API* glBlendFunc)(GLenum, GLenum) = nullptr;
void (REIM_GL_API* glCullFace)(GLenum) = nullptr;
void (REIM_GL_API* glFrontFace)(GLenum) = nullptr;
void (REIM_GL_API* glScissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
void (REIM_GL_API* glPolygonMode)(GLenum, GLenum) = nullptr;
void (REIM_GL_API* glLineWidth)(GLfloat) = nullptr;
const GLubyte* (REIM_GL_API* glGetString)(GLenum) = nullptr;
void (REIM_GL_API* glGetIntegerv)(GLenum, GLint*) = nullptr;
void (REIM_GL_API* glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) =
    nullptr;
void (REIM_GL_API* glPixelStorei)(GLenum, GLint) = nullptr;
void (REIM_GL_API* glDrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
void (REIM_GL_API* glDrawArrays)(GLenum, GLint, GLsizei) = nullptr;
void (REIM_GL_API* glGenTextures)(GLsizei, GLuint*) = nullptr;
void (REIM_GL_API* glBindTexture)(GLenum, GLuint) = nullptr;
void (REIM_GL_API* glDeleteTextures)(GLsizei, const GLuint*) = nullptr;
void (REIM_GL_API* glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                                 GLenum, const void*) = nullptr;
void (REIM_GL_API* glTexParameteri)(GLenum, GLenum, GLint) = nullptr;

void (REIM_GL_API* glGenVertexArrays)(GLsizei, GLuint*) = nullptr;
void (REIM_GL_API* glBindVertexArray)(GLuint) = nullptr;
void (REIM_GL_API* glDeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
void (REIM_GL_API* glGenBuffers)(GLsizei, GLuint*) = nullptr;
void (REIM_GL_API* glBindBuffer)(GLenum, GLuint) = nullptr;
void (REIM_GL_API* glBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
void (REIM_GL_API* glDeleteBuffers)(GLsizei, const GLuint*) = nullptr;
void (REIM_GL_API* glEnableVertexAttribArray)(GLuint) = nullptr;
void (REIM_GL_API* glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                          const void*) = nullptr;
void (REIM_GL_API* glVertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const void*) =
    nullptr;
GLuint (REIM_GL_API* glCreateShader)(GLenum) = nullptr;
void (REIM_GL_API* glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) =
    nullptr;
void (REIM_GL_API* glCompileShader)(GLuint) = nullptr;
void (REIM_GL_API* glGetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
void (REIM_GL_API* glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
GLuint (REIM_GL_API* glCreateProgram)(void) = nullptr;
void (REIM_GL_API* glAttachShader)(GLuint, GLuint) = nullptr;
void (REIM_GL_API* glLinkProgram)(GLuint) = nullptr;
void (REIM_GL_API* glGetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
void (REIM_GL_API* glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
void (REIM_GL_API* glDeleteShader)(GLuint) = nullptr;
void (REIM_GL_API* glDeleteProgram)(GLuint) = nullptr;
void (REIM_GL_API* glUseProgram)(GLuint) = nullptr;
GLint (REIM_GL_API* glGetUniformLocation)(GLuint, const GLchar*) = nullptr;
void (REIM_GL_API* glUniform1i)(GLint, GLint) = nullptr;
void (REIM_GL_API* glUniform1f)(GLint, GLfloat) = nullptr;
void (REIM_GL_API* glUniform2f)(GLint, GLfloat, GLfloat) = nullptr;
void (REIM_GL_API* glUniform3f)(GLint, GLfloat, GLfloat, GLfloat) = nullptr;
void (REIM_GL_API* glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
void (REIM_GL_API* glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;
void (REIM_GL_API* glActiveTexture)(GLenum) = nullptr;

namespace {

#define REIM_LOAD_GL(name)                                             \
  do {                                                                 \
    name = reinterpret_cast<decltype(name)>(glfwGetProcAddress(#name)); \
    if (name == nullptr) {                                             \
      error = "missing OpenGL entry point: " #name;                    \
      return false;                                                    \
    }                                                                  \
  } while (false)

} // namespace

bool load_gl_functions(std::string& error) {
  REIM_LOAD_GL(glClear);
  REIM_LOAD_GL(glGetError);
  REIM_LOAD_GL(glClearColor);
  REIM_LOAD_GL(glViewport);
  REIM_LOAD_GL(glEnable);
  REIM_LOAD_GL(glDisable);
  REIM_LOAD_GL(glDepthFunc);
  REIM_LOAD_GL(glDepthMask);
  REIM_LOAD_GL(glBlendFunc);
  REIM_LOAD_GL(glCullFace);
  REIM_LOAD_GL(glFrontFace);
  REIM_LOAD_GL(glScissor);
  REIM_LOAD_GL(glPolygonMode);
  REIM_LOAD_GL(glLineWidth);
  REIM_LOAD_GL(glGetString);
  REIM_LOAD_GL(glGetIntegerv);
  REIM_LOAD_GL(glReadPixels);
  REIM_LOAD_GL(glPixelStorei);
  REIM_LOAD_GL(glDrawElements);
  REIM_LOAD_GL(glDrawArrays);
  REIM_LOAD_GL(glGenTextures);
  REIM_LOAD_GL(glBindTexture);
  REIM_LOAD_GL(glDeleteTextures);
  REIM_LOAD_GL(glTexImage2D);
  REIM_LOAD_GL(glTexParameteri);
  REIM_LOAD_GL(glGenVertexArrays);
  REIM_LOAD_GL(glBindVertexArray);
  REIM_LOAD_GL(glDeleteVertexArrays);
  REIM_LOAD_GL(glGenBuffers);
  REIM_LOAD_GL(glBindBuffer);
  REIM_LOAD_GL(glBufferData);
  REIM_LOAD_GL(glDeleteBuffers);
  REIM_LOAD_GL(glEnableVertexAttribArray);
  REIM_LOAD_GL(glVertexAttribPointer);
  REIM_LOAD_GL(glVertexAttribIPointer);
  REIM_LOAD_GL(glCreateShader);
  REIM_LOAD_GL(glShaderSource);
  REIM_LOAD_GL(glCompileShader);
  REIM_LOAD_GL(glGetShaderiv);
  REIM_LOAD_GL(glGetShaderInfoLog);
  REIM_LOAD_GL(glCreateProgram);
  REIM_LOAD_GL(glAttachShader);
  REIM_LOAD_GL(glLinkProgram);
  REIM_LOAD_GL(glGetProgramiv);
  REIM_LOAD_GL(glGetProgramInfoLog);
  REIM_LOAD_GL(glDeleteShader);
  REIM_LOAD_GL(glDeleteProgram);
  REIM_LOAD_GL(glUseProgram);
  REIM_LOAD_GL(glGetUniformLocation);
  REIM_LOAD_GL(glUniform1i);
  REIM_LOAD_GL(glUniform1f);
  REIM_LOAD_GL(glUniform2f);
  REIM_LOAD_GL(glUniform3f);
  REIM_LOAD_GL(glUniform4f);
  REIM_LOAD_GL(glUniformMatrix4fv);
  REIM_LOAD_GL(glActiveTexture);
  return true;
}
