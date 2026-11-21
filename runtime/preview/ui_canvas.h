/*
 * REIMPreview v3 UI canvas (M3-A5).
 *
 * Draws the Runtime's ReimUiFrame display list: solid rects, textured quads and
 * glyph quads, with per-draw clip rectangles and the Runtime cursor. It never
 * lays out widgets or decides hit targets; those stay in the Runtime.
 */
#pragma once

#include "gl_loader.h"
#include "reim_ui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reim::preview {

class UiCanvas {
 public:
  UiCanvas() = default;
  ~UiCanvas();

  bool initialize(const uint8_t* block_png, size_t block_size,
                  const uint8_t* font_png, size_t font_size, std::string& error);
  void shutdown();

  /* Call before release_frame: the draw/text arrays live in the frame blob. */
  void draw(const ReimUiFrame& ui, int fb_width, int fb_height);

 private:
  struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;
  };

  bool upload_texture(const uint8_t* png, size_t size, GLuint& out,
                      std::string& error);
  void draw_quad(float x, float y, float w, float h, float u0, float v0,
                 float u1, float v1, uint32_t color, GLuint texture);
  void draw_clipped(const ReimUiDraw& draw, int fb_width, int fb_height);

  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint white_ = 0;
  GLuint block_ = 0;
  GLuint font_ = 0;
  GLint screen_ = -1;
  GLint texture_ = -1;
  int fb_width_ = 1;
  int fb_height_ = 1;
  float scale_ = 1.0f;
  bool logged_first_ = false;
  std::vector<Vertex> vertices_;
};

} // namespace reim::preview
