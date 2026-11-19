#include "util/png.h"

#include "util/file.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace reim::png {

bool load_memory(const uint8_t* data, size_t size, int desired_channels,
                 Image& out, std::string& error) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width,
                                          &height, &channels, desired_channels);
  if (!pixels) {
    error = std::string("PNG decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown");
    return false;
  }
  out.width = width;
  out.height = height;
  out.channels = desired_channels;
  out.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * desired_channels);
  stbi_image_free(pixels);
  return true;
}

bool load_file(const std::string& path_utf8, int desired_channels, Image& out,
               std::string& error) {
  file::Bytes bytes;
  if (!file::read_all(file::from_utf8(path_utf8), bytes, error)) return false;
  return load_memory(bytes.data(), bytes.size(), desired_channels, out, error);
}

bool write_file(const std::string& path_utf8, int width, int height, int channels,
                const uint8_t* pixels, std::string& error) {
  std::error_code ec;
  const auto path = file::from_utf8(path_utf8);
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "cannot create directory: " + path_utf8 + ": " + ec.message();
      return false;
    }
  }
  const int written = stbi_write_png(path_utf8.c_str(), width, height, channels,
                                     pixels, width * channels);
  if (!written) {
    error = "PNG write failed: " + path_utf8;
    return false;
  }
  return true;
}

} // namespace reim::png
