#include "hud.h"

#include <array>
#include <cstddef>
#include <vector>

namespace reim::preview {
namespace {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kCellWidth = 6;
constexpr int kCellHeight = 8;
constexpr int kColumns = 16;
constexpr int kRows = 6;
constexpr int kCells = kColumns * kRows;  // 96: ASCII 32..126 + one white cell
constexpr int kAtlasWidth = kColumns * kCellWidth;
constexpr int kAtlasHeight = kRows * kCellHeight;
constexpr int kWhiteCell = 95;

/* Public-domain 5x7 column-major font, low bit = top row. */
constexpr unsigned char kFont[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02}, {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00}, {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x08, 0x04, 0x08, 0x10, 0x08},
};

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
uniform sampler2D u_font;
out vec4 frag_color;
void main() {
  float mask = texture(u_font, v_uv).a;
  frag_color = vec4(v_color.rgb, v_color.a * mask);
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
    error = std::string("HUD shader compile failed: ") + log.data();
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

} // namespace

Hud::~Hud() { shutdown(); }

bool Hud::initialize(std::string& error) {
  std::vector<unsigned char> atlas(static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
  auto put = [&](int x, int y, unsigned char value) {
    if (x < 0 || y < 0 || x >= kAtlasWidth || y >= kAtlasHeight) return;
    const size_t offset = (static_cast<size_t>(y) * kAtlasWidth + x) * 4;
    atlas[offset + 0] = 255;
    atlas[offset + 1] = 255;
    atlas[offset + 2] = 255;
    atlas[offset + 3] = value;
  };
  for (int index = 0; index < 95; ++index) {
    const int cell_x = (index % kColumns) * kCellWidth;
    const int cell_y = (index / kColumns) * kCellHeight;
    for (int column = 0; column < kGlyphWidth; ++column) {
      const unsigned char bits = kFont[index][column];
      for (int row = 0; row < kGlyphHeight; ++row) {
        if ((bits >> row) & 1u) put(cell_x + column, cell_y + row, 255);
      }
    }
  }
  const int white_x = (kWhiteCell % kColumns) * kCellWidth;
  const int white_y = (kWhiteCell / kColumns) * kCellHeight;
  for (int y = 0; y < kGlyphHeight; ++y) {
    for (int x = 0; x < kGlyphWidth; ++x) put(white_x + x, white_y + y, 255);
  }

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasWidth, kAtlasHeight, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, atlas.data());
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
    error = std::string("HUD program link failed: ") + log.data();
    return false;
  }
  screen_location_ = glGetUniformLocation(program_, "u_screen");
  font_location_ = glGetUniformLocation(program_, "u_font");

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
  vertices_.reserve(4096);
  return true;
}

void Hud::shutdown() {
  if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
  if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
  if (texture_ != 0) glDeleteTextures(1, &texture_);
  if (program_ != 0) glDeleteProgram(program_);
  vbo_ = vao_ = texture_ = program_ = 0;
}

void Hud::begin(int width, int height) {
  width_ = width > 0 ? width : 1;
  height_ = height > 0 ? height : 1;
  vertices_.clear();
}

float Hud::text_width(std::string_view value, float scale) {
  return static_cast<float>(value.size()) * kCellWidth * scale;
}

void Hud::push_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                    float v1, uint32_t argb) {
  const float a = static_cast<float>((argb >> 24) & 0xFFu) / 255.0f;
  const float r = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
  const float g = static_cast<float>((argb >> 8) & 0xFFu) / 255.0f;
  const float b = static_cast<float>(argb & 0xFFu) / 255.0f;
  const Vertex v00{x0, y0, u0, v0, r, g, b, a};
  const Vertex v10{x1, y0, u1, v0, r, g, b, a};
  const Vertex v11{x1, y1, u1, v1, r, g, b, a};
  const Vertex v01{x0, y1, u0, v1, r, g, b, a};
  vertices_.push_back(v00);
  vertices_.push_back(v10);
  vertices_.push_back(v11);
  vertices_.push_back(v00);
  vertices_.push_back(v11);
  vertices_.push_back(v01);
}

void Hud::text(float x, float y, float scale, uint32_t argb, std::string_view value) {
  float cursor = x;
  const float glyph_w = kGlyphWidth * scale;
  const float glyph_h = kGlyphHeight * scale;
  for (char raw : value) {
    char c = raw;
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    if (c < 32 || c > 126) c = '?';
    if (c == ' ') {
      cursor += kCellWidth * scale;
      continue;
    }
    const int index = c - 32;
    const int cell_x = (index % kColumns) * kCellWidth;
    const int cell_y = (index / kColumns) * kCellHeight;
    const float u0 = (static_cast<float>(cell_x) + 0.5f) / kAtlasWidth;
    const float v0 = (static_cast<float>(cell_y) + 0.5f) / kAtlasHeight;
    const float u1 = (static_cast<float>(cell_x + kGlyphWidth - 1) + 0.5f) / kAtlasWidth;
    const float v1 = (static_cast<float>(cell_y + kGlyphHeight - 1) + 0.5f) / kAtlasHeight;
    push_quad(cursor, y, cursor + glyph_w, y + glyph_h, u0, v0, u1, v1, argb);
    cursor += kCellWidth * scale;
  }
}

void Hud::rect(float x, float y, float width, float height, uint32_t argb) {
  const int cell_x = (kWhiteCell % kColumns) * kCellWidth;
  const int cell_y = (kWhiteCell / kColumns) * kCellHeight;
  const float u = (static_cast<float>(cell_x) + 2.5f) / kAtlasWidth;
  const float v = (static_cast<float>(cell_y) + 3.5f) / kAtlasHeight;
  push_quad(x, y, x + width, y + height, u, v, u, v, argb);
}

void Hud::end() {
  if (vertices_.empty()) return;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(program_);
  glUniform2f(static_cast<GLint>(screen_location_), static_cast<float>(width_),
              static_cast<float>(height_));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glUniform1i(font_location_, 0);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
               vertices_.data(), GL_DYNAMIC_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

} // namespace reim::preview
