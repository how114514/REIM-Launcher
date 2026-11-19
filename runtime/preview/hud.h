/*
 * Minimal 2D HUD for REIMPreview (M1-F6): FPS/TPS, position, chunk and draw
 * statistics, loading progress and control hints.
 *
 * The font is a built-in 5x7 bitmap (no external asset), rendered as one
 * batched triangle list in a single draw call.
 */
#pragma once

#include "gl_loader.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reim::preview {

class Hud {
 public:
  Hud() = default;
  ~Hud();

  bool initialize(std::string& error);
  void shutdown();

  void begin(int width, int height);
  /* x/y are pixels from the top-left. `argb` is 0xAARRGGBB. */
  void text(float x, float y, float scale, uint32_t argb, std::string_view value);
  void rect(float x, float y, float width, float height, uint32_t argb);
  void end();

  static float text_width(std::string_view value, float scale);

 private:
  struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;
  };

  void push_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                 float v1, uint32_t argb);

  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint texture_ = 0;
  GLint screen_location_ = -1;
  GLint font_location_ = -1;
  int width_ = 1;
  int height_ = 1;
  std::vector<Vertex> vertices_;
};

} // namespace reim::preview
