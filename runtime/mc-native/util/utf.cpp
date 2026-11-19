#include "util/utf.h"

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace reim::utf {
namespace {

void append_utf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

} // namespace

std::string wide_to_utf8(std::wstring_view value) {
#ifdef _WIN32
  if (value.empty()) return {};
  const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<size_t>(size), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()), out.data(), size,
                        nullptr, nullptr);
  return out;
#else
  std::string out;
  out.reserve(value.size());
  for (wchar_t wc : value) {
    const uint32_t cp = static_cast<uint32_t>(wc);
    append_utf8(out, cp);
  }
  return out;
#endif
}

std::wstring utf8_to_wide(std::string_view value) {
#ifdef _WIN32
  if (value.empty()) return {};
  const int size = ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<size_t>(size), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()), out.data(), size);
  return out;
#else
  std::wstring out;
  out.reserve(value.size());
  for (char c : value) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
  return out;
#endif
}

std::string modified_utf8_to_utf8(const char* data, size_t size) {
  std::string out;
  out.reserve(size);
  size_t i = 0;
  while (i < size) {
    const uint8_t b = static_cast<uint8_t>(data[i]);
    if (b == 0x00) {
      out.push_back('\0');
      ++i;
      continue;
    }
    if ((b & 0x80) == 0) {
      out.push_back(static_cast<char>(b));
      ++i;
      continue;
    }
    if ((b & 0xE0) == 0xC0 && i + 1 < size) {
      const uint32_t cp = ((b & 0x1F) << 6) |
                          (static_cast<uint8_t>(data[i + 1]) & 0x3F);
      append_utf8(out, cp);
      i += 2;
      continue;
    }
    if ((b & 0xF0) == 0xE0 && i + 2 < size) {
      const uint32_t cp = ((b & 0x0F) << 12) |
                          ((static_cast<uint8_t>(data[i + 1]) & 0x3F) << 6) |
                          (static_cast<uint8_t>(data[i + 2]) & 0x3F);
      const bool high_surrogate = cp >= 0xD800 && cp <= 0xDBFF;
      if (high_surrogate && i + 5 < size &&
          (static_cast<uint8_t>(data[i + 3]) & 0xF0) == 0xE0) {
        const uint32_t low = ((static_cast<uint8_t>(data[i + 3]) & 0x0F) << 12) |
                             ((static_cast<uint8_t>(data[i + 4]) & 0x3F) << 6) |
                             (static_cast<uint8_t>(data[i + 5]) & 0x3F);
        if (low >= 0xDC00 && low <= 0xDFFF) {
          const uint32_t combined = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          append_utf8(out, combined);
          i += 6;
          continue;
        }
      }
      append_utf8(out, cp);
      i += 3;
      continue;
    }
    if ((b & 0xF8) == 0xF0 && i + 3 < size) {
      const uint32_t cp = ((b & 0x07) << 18) |
                          ((static_cast<uint8_t>(data[i + 1]) & 0x3F) << 12) |
                          ((static_cast<uint8_t>(data[i + 2]) & 0x3F) << 6) |
                          (static_cast<uint8_t>(data[i + 3]) & 0x3F);
      append_utf8(out, cp);
      i += 4;
      continue;
    }
    // Invalid byte: replace rather than abort the whole NBT document.
    out.push_back('?');
    ++i;
  }
  return out;
}

} // namespace reim::utf
