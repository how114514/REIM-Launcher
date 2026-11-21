#include "ui_canvas.h"

#include "util/log.h"
#include "util/png.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace reim::preview {
namespace {

const char* const kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_screen;
out vec2 v_uv;
out vec4 v_color;
void main() {
  v_uv = a_uv;
  v_color = a_color;
  gl_Position = vec4(a_position.x / u_screen.x * 2.0 - 1.0,
                     1.0 - a_position.y / u_screen.y * 2.0, 0.0, 1.0);
}
)GLSL";

const char* const kFragmentShader = R"GLSL(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_texture;
out vec4 frag_color;
void main() {
  vec4 texel = texture(u_texture, v_uv);
  frag_color = texel * v_color;
  if (frag_color.a < 0.004) discard;
}
)GLSL";

GLuint compile(GLenum type, const char* source, std::string& error) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<GLchar> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    error = std::string("UI shader compile failed: ") + log.data();
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

} // namespace

UiCanvas::~UiCanvas() { shutdown(); }

bool UiCanvas::initialize(const uint8_t* block_png, size_t block_size,
                          const uint8_t* font_png, size_t font_size,
                          std::string& error) {
  if (!upload_texture(block_png, block_size, block_, error)) return false;
  if (!upload_texture(font_png, font_size, font_, error)) return false;

  const uint8_t white_pixels[4] = {255, 255, 255, 255};
  glGenTextures(1, &white_);
  glBindTexture(GL_TEXTURE_2D, white_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               white_pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader, error);
  if (vertex == 0) return false;
  const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader, error);
  if (fragment == 0) {
    glDeleteShader(vertex);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vertex);
  glAttachShader(program_, fragment);
  glLinkProgram(program_);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint status = GL_FALSE;
  glGetProgramiv(program_, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
    std::vector<GLchar> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
    glGetProgramInfoLog(program_, static_cast<GLsizei>(log.size()), nullptr, log.data());
    error = std::string("UI program link failed: ") + log.data();
    return false;
  }
  screen_ = glGetUniformLocation(program_, "u_screen");
  texture_ = glGetUniformLocation(program_, "u_texture");
  if (screen_ < 0 || texture_ < 0) {
    error = "UI shader uniforms were optimized away";
    return false;
  }

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<const void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<const void*>(offsetof(Vertex, u)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<const void*>(offsetof(Vertex, r)));
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
  vertices_.reserve(6 * 512);
  return true;
}

bool UiCanvas::upload_texture(const uint8_t* png, size_t size, GLuint& out,
                              std::string& error) {
  reim::png::Image image;
  if (!reim::png::load_memory(png, size, 4, image, error)) return false;
  glGenTextures(1, &out);
  glBindTexture(GL_TEXTURE_2D, out);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, image.pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return true;
}

void UiCanvas::shutdown() {
  if (white_ != 0) glDeleteTextures(1, &white_);
  if (block_ != 0) glDeleteTextures(1, &block_);
  if (font_ != 0) glDeleteTextures(1, &font_);
  if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
  if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
  if (program_ != 0) glDeleteProgram(program_);
  white_ = block_ = font_ = vbo_ = vao_ = program_ = 0;
}

void UiCanvas::draw_quad(float x, float y, float w, float h, float u0, float v0,
                         float u1, float v1, uint32_t color, GLuint texture) {
  const float r = static_cast<float>(color & 0xFFu) / 255.0f;
  const float g = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
  const float b = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
  const float a = static_cast<float>((color >> 24) & 0xFFu) / 255.0f;
  const float x0 = x * scale_;
  const float y0 = y * scale_;
  const float x1 = (x + w) * scale_;
  const float y1 = (y + h) * scale_;
  vertices_.clear();
  const float uvs[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
  const float xs[6] = {x0, x1, x1, x0, x1, x0};
  const float ys[6] = {y0, y0, y1, y0, y1, y1};
  for (int i = 0; i < 6; ++i) {
    Vertex vertex{};
    vertex.x = xs[i];
    vertex.y = ys[i];
    vertex.u = uvs[i][0] != 0.0f ? u1 : u0;
    vertex.v = uvs[i][1] != 0.0f ? v1 : v0;
    vertex.r = r;
    vertex.g = g;
    vertex.b = b;
    vertex.a = a;
    vertices_.push_back(vertex);
  }
  glBindTexture(GL_TEXTURE_2D, texture);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
               vertices_.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
}

void UiCanvas::draw(const ReimUiFrame& ui, int fb_width, int fb_height) {
  if (program_ == 0 || ui.draw_count == 0) return;
  fb_width_ = std::max(1, fb_width);
  fb_height_ = std::max(1, fb_height);
  scale_ = static_cast<float>(std::max(1u, ui.ui_scale));
  const bool first_call = !logged_first_;
  if (first_call) {
    logged_first_ = true;
    reim::log::info("preview",
                    "UI canvas: draws=" + std::to_string(ui.draw_count) +
                        " screen=" + std::to_string(ui.screen_id) +
                        " scale=" + std::to_string(ui.ui_scale) +
                        " canvas=" + std::to_string(ui.canvas_width) + "x" +
                        std::to_string(ui.canvas_height));
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(program_);
  glUniform2f(screen_, static_cast<float>(fb_width_),
              static_cast<float>(fb_height_));
  glUniform1i(texture_, 0);
  glActiveTexture(GL_TEXTURE0);

  for (uint32_t i = 0; i < ui.draw_count; ++i) {
    const ReimUiDraw& item = ui.draws[i];
    const bool clip = (item.flags & REIM_UI_FLAG_CLIP) != 0 && item.clip_w > 0.0f &&
                      item.clip_h > 0.0f;
    if (clip) {
      const int sx = static_cast<int>(std::floor(item.clip_x * scale_));
      const int sy = static_cast<int>(
          std::floor(fb_height_ - (item.clip_y + item.clip_h) * scale_));
      const int sw = static_cast<int>(std::ceil(item.clip_w * scale_));
      const int sh = static_cast<int>(std::ceil(item.clip_h * scale_));
      glEnable(GL_SCISSOR_TEST);
      glScissor(std::max(0, sx), std::max(0, sy), std::max(0, sw),
                std::max(0, sh));
    } else {
      glDisable(GL_SCISSOR_TEST);
    }

    GLuint texture = white_;
    if (item.texture == REIM_UI_TEXTURE_FONT_ATLAS) texture = font_;
    else if (item.texture == REIM_UI_TEXTURE_BLOCK_ATLAS) texture = block_;
    else if (item.texture == REIM_UI_TEXTURE_ITEM_ATLAS) texture = block_;
    else if (item.texture == REIM_UI_TEXTURE_UI_ATLAS) texture = block_;
    draw_quad(item.x, item.y, item.w, item.h, item.u0, item.v0, item.u1, item.v1,
              item.color, texture);
  }
  glDisable(GL_SCISSOR_TEST);

  /* Cursor: the Runtime owns its position and mode; the Host only draws it. */
  if (ui.cursor_mode != REIM_UI_CURSOR_HIDDEN) {
    const float cx = ui.cursor_x * scale_;
    const float cy = ui.cursor_y * scale_;
    const float s = 6.0f;
    draw_quad((cx - s) / scale_, (cy - 1.0f) / scale_, (2.0f * s) / scale_,
              2.0f / scale_, 0, 0, 1, 1, 0xF0FFFFFFu, white_);
    draw_quad((cx - 1.0f) / scale_, (cy - s) / scale_, 2.0f / scale_,
              (2.0f * s) / scale_, 0, 0, 1, 1, 0xF0FFFFFFu, white_);
  }

  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

} // namespace reim::preview
