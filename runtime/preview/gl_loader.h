/*
 * Minimal OpenGL 3.3 function loader for REIMPreview.
 *
 * The M1 dependency set is fixed (docs/M1_PLAN.md M1-D11); glad is not vendored,
 * so the temporary host loads the small surface it needs through
 * glfwGetProcAddress. No GL/gl.h is included here: these names and constants
 * are private to the preview host.
 *
 * Call load_gl_functions() once with a current OpenGL 3.3 context.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#  define REIM_GL_API __stdcall
#else
#  define REIM_GL_API
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;
typedef unsigned char GLubyte;
typedef void GLvoid;

/* Constants (OpenGL 3.3 core). */
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_CCW 0x0901
#define GL_CW 0x0900

/* OpenGL 1.1 (available from opengl32.dll; glfwGetProcAddress resolves it). */
extern void (REIM_GL_API* glClear)(GLbitfield mask);
extern GLenum (REIM_GL_API* glGetError)(void);
extern void (REIM_GL_API* glClearColor)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
extern void (REIM_GL_API* glViewport)(GLint x, GLint y, GLsizei w, GLsizei h);
extern void (REIM_GL_API* glEnable)(GLenum cap);
extern void (REIM_GL_API* glDisable)(GLenum cap);
extern void (REIM_GL_API* glDepthFunc)(GLenum func);
extern void (REIM_GL_API* glDepthMask)(GLboolean flag);
extern void (REIM_GL_API* glBlendFunc)(GLenum sfactor, GLenum dfactor);
extern void (REIM_GL_API* glCullFace)(GLenum mode);
extern void (REIM_GL_API* glFrontFace)(GLenum mode);
extern void (REIM_GL_API* glScissor)(GLint x, GLint y, GLsizei w, GLsizei h);
extern void (REIM_GL_API* glPolygonMode)(GLenum face, GLenum mode);
extern void (REIM_GL_API* glLineWidth)(GLfloat width);
extern const GLubyte* (REIM_GL_API* glGetString)(GLenum name);
extern void (REIM_GL_API* glGetIntegerv)(GLenum pname, GLint* data);
extern void (REIM_GL_API* glReadPixels)(GLint x, GLint y, GLsizei w, GLsizei h,
                                        GLenum format, GLenum type, void* pixels);
extern void (REIM_GL_API* glPixelStorei)(GLenum pname, GLint param);
extern void (REIM_GL_API* glDrawElements)(GLenum mode, GLsizei count, GLenum type,
                                          const void* indices);
extern void (REIM_GL_API* glDrawArrays)(GLenum mode, GLint first, GLsizei count);
extern void (REIM_GL_API* glGenTextures)(GLsizei n, GLuint* textures);
extern void (REIM_GL_API* glBindTexture)(GLenum target, GLuint texture);
extern void (REIM_GL_API* glDeleteTextures)(GLsizei n, const GLuint* textures);
extern void (REIM_GL_API* glTexImage2D)(GLenum target, GLint level, GLint internal_format,
                                        GLsizei width, GLsizei height, GLint border,
                                        GLenum format, GLenum type, const void* pixels);
extern void (REIM_GL_API* glTexParameteri)(GLenum target, GLenum pname, GLint param);

/* OpenGL 2.0 / 3.0 / 3.3 core entry points. */
extern void (REIM_GL_API* glGenVertexArrays)(GLsizei n, GLuint* arrays);
extern void (REIM_GL_API* glBindVertexArray)(GLuint array);
extern void (REIM_GL_API* glDeleteVertexArrays)(GLsizei n, const GLuint* arrays);
extern void (REIM_GL_API* glGenBuffers)(GLsizei n, GLuint* buffers);
extern void (REIM_GL_API* glBindBuffer)(GLenum target, GLuint buffer);
extern void (REIM_GL_API* glBufferData)(GLenum target, GLsizeiptr size, const void* data,
                                        GLenum usage);
extern void (REIM_GL_API* glDeleteBuffers)(GLsizei n, const GLuint* buffers);
extern void (REIM_GL_API* glEnableVertexAttribArray)(GLuint index);
extern void (REIM_GL_API* glVertexAttribPointer)(GLuint index, GLint size, GLenum type,
                                                  GLboolean normalized, GLsizei stride,
                                                  const void* pointer);
extern void (REIM_GL_API* glVertexAttribIPointer)(GLuint index, GLint size, GLenum type,
                                                  GLsizei stride, const void* pointer);
extern GLuint (REIM_GL_API* glCreateShader)(GLenum type);
extern void (REIM_GL_API* glShaderSource)(GLuint shader, GLsizei count,
                                           const GLchar* const* string, const GLint* length);
extern void (REIM_GL_API* glCompileShader)(GLuint shader);
extern void (REIM_GL_API* glGetShaderiv)(GLuint shader, GLenum pname, GLint* params);
extern void (REIM_GL_API* glGetShaderInfoLog)(GLuint shader, GLsizei buf_size,
                                               GLsizei* length, GLchar* info_log);
extern GLuint (REIM_GL_API* glCreateProgram)(void);
extern void (REIM_GL_API* glAttachShader)(GLuint program, GLuint shader);
extern void (REIM_GL_API* glLinkProgram)(GLuint program);
extern void (REIM_GL_API* glGetProgramiv)(GLuint program, GLenum pname, GLint* params);
extern void (REIM_GL_API* glGetProgramInfoLog)(GLuint program, GLsizei buf_size,
                                                GLsizei* length, GLchar* info_log);
extern void (REIM_GL_API* glDeleteShader)(GLuint shader);
extern void (REIM_GL_API* glDeleteProgram)(GLuint program);
extern void (REIM_GL_API* glUseProgram)(GLuint program);
extern GLint (REIM_GL_API* glGetUniformLocation)(GLuint program, const GLchar* name);
extern void (REIM_GL_API* glUniform1i)(GLint location, GLint v0);
extern void (REIM_GL_API* glUniform1f)(GLint location, GLfloat v0);
extern void (REIM_GL_API* glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
extern void (REIM_GL_API* glUniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
extern void (REIM_GL_API* glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                                      GLfloat v3);
extern void (REIM_GL_API* glUniformMatrix4fv)(GLint location, GLsizei count,
                                               GLboolean transpose, const GLfloat* value);
extern void (REIM_GL_API* glActiveTexture)(GLenum texture);

/* Returns false and sets `error` to the first unresolved name. */
bool load_gl_functions(std::string& error);
