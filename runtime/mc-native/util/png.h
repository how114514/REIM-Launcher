#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reim::png {

struct Image {
  int width = 0;
  int height = 0;
  int channels = 0;
  std::vector<uint8_t> pixels;

  const uint8_t* at(int x, int y) const {
    return pixels.data() + (static_cast<size_t>(y) * width + x) * channels;
  }
};

bool load_memory(const uint8_t* data, size_t size, int desired_channels,
                 Image& out, std::string& error);
bool load_file(const std::string& path_utf8, int desired_channels, Image& out,
               std::string& error);
bool write_file(const std::string& path_utf8, int width, int height, int channels,
                const uint8_t* pixels, std::string& error);

/* Encodes in memory (RGBA8 or the given channel count). Used by get_asset for
 * synthesized v3 assets such as the Runtime font atlas. */
bool encode_memory(int width, int height, int channels, const uint8_t* pixels,
                   std::vector<uint8_t>& out, std::string& error);

} // namespace reim::png
