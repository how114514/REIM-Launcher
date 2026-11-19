#include "util/file.h"

#include <fstream>
#include <system_error>

namespace reim::file {

bool exists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec) && !ec;
}

bool is_directory(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec) && !ec;
}

bool read_all(const std::filesystem::path& path, Bytes& out, std::string& error) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    error = "cannot open file: " + to_utf8(path);
    return false;
  }
  const std::streamoff size = stream.tellg();
  if (size < 0) {
    error = "cannot size file: " + to_utf8(path);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  stream.seekg(0, std::ios::beg);
  if (size > 0) {
    stream.read(reinterpret_cast<char*>(out.data()), size);
    if (!stream) {
      error = "short read: " + to_utf8(path);
      return false;
    }
  }
  return true;
}

bool read_text(const std::filesystem::path& path, std::string& out, std::string& error) {
  Bytes bytes;
  if (!read_all(path, bytes, error)) return false;
  out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

bool write_all(const std::filesystem::path& path, const void* data, size_t size,
               std::string& error) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "cannot create directory: " + to_utf8(path.parent_path()) + ": " + ec.message();
      return false;
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "cannot open for write: " + to_utf8(path);
    return false;
  }
  if (size > 0) {
    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  }
  if (!stream) {
    error = "write failed: " + to_utf8(path);
    return false;
  }
  return true;
}

bool write_text(const std::filesystem::path& path, const std::string& text,
                std::string& error) {
  return write_all(path, text.data(), text.size(), error);
}

bool ensure_directory(const std::filesystem::path& path, std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    error = "cannot create directory: " + to_utf8(path) + ": " + ec.message();
    return false;
  }
  return true;
}

std::string to_utf8(const std::filesystem::path& path) {
  std::filesystem::path normalized = path;
  normalized.make_preferred();
  const std::u8string value = normalized.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path from_utf8(const std::string& path) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()),
                                             path.size()));
}

std::filesystem::path join(const std::filesystem::path& base, const char* child) {
  std::filesystem::path out = base;
  out /= child;
  return out;
}

} // namespace reim::file
