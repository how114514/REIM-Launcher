#include "util/file.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

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

bool write_atomic(const std::filesystem::path& path, const void* data, size_t size,
                  std::string& error) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "cannot create directory: " + to_utf8(path.parent_path()) + ": " +
              ec.message();
      return false;
    }
  }
  static std::atomic<uint32_t> counter{0};
  std::filesystem::path temp = path;
  temp += ".tmp-" + std::to_string(++counter);
#ifdef _WIN32
  const std::wstring temp_wide = temp.wstring();
  const std::wstring target_wide = path.wstring();
  HANDLE handle = ::CreateFileW(temp_wide.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = "cannot create temp file: " + to_utf8(temp) + " (" +
            std::to_string(::GetLastError()) + ")";
    return false;
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  size_t written = 0;
  bool ok = true;
  while (written < size) {
    const DWORD chunk = static_cast<DWORD>(
        std::min<size_t>(size - written, 64u * 1024u * 1024u));
    DWORD count = 0;
    if (!::WriteFile(handle, bytes + written, chunk, &count, nullptr) ||
        count == 0) {
      error = "write failed: " + to_utf8(temp) + " (" +
              std::to_string(::GetLastError()) + ")";
      ok = false;
      break;
    }
    written += count;
  }
  if (ok && !::FlushFileBuffers(handle)) {
    error = "flush failed: " + to_utf8(temp) + " (" +
            std::to_string(::GetLastError()) + ")";
    ok = false;
  }
  ::CloseHandle(handle);
  if (!ok) {
    std::filesystem::remove(temp, ec);
    return false;
  }
  if (!::MoveFileExW(temp_wide.c_str(), target_wide.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = "atomic replace failed: " + to_utf8(path) + " (" +
            std::to_string(::GetLastError()) + ")";
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
#else
  if (!write_all(temp, data, size, error)) return false;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    error = "atomic replace failed: " + to_utf8(path) + ": " + ec.message();
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
#endif
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
