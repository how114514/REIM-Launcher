#include "common.h"

#include "util/file.h"
#include "util/log.h"
#include "util/utf.h"

#include <cstdio>
#include <format>

#ifdef _WIN32
#include <windows.h>
#endif

namespace reim::conv {

Args parse_args(int argc, char** argv, int start_index) {
  Args args;
  for (int i = start_index; i < argc; ++i) {
    std::string key = argv[i];
    if (key.rfind("--", 0) != 0) continue;
    key = key.substr(2);
    if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
      args.values.emplace_back(std::move(key), argv[++i]);
    } else {
      args.values.emplace_back(std::move(key), "");
    }
  }
  return args;
}

Json parse_json_noexcept(const std::string& text) {
  return Json::parse(text.begin(), text.end(), nullptr, false);
}

Json parse_json_noexcept(std::string_view text) {
  return Json::parse(text.begin(), text.end(), nullptr, false);
}

bool read_json_file(const std::filesystem::path& path, Json& out, std::string& error) {
  std::string text;
  if (!file::read_text(path, text, error)) return false;
  out = parse_json_noexcept(text);
  if (out.is_discarded()) {
    error = "invalid JSON: " + file::to_utf8(path);
    return false;
  }
  return true;
}

bool write_json_file(const std::filesystem::path& path, const Json& value,
                     std::string& error) {
  return write_text_file(path, dump(value), error);
}

bool write_text_file(const std::filesystem::path& path, const std::string& text,
                     std::string& error) {
  return file::write_text(path, text, error);
}

std::string exe_directory_utf8() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0) return ".";
  const std::u8string value =
      std::filesystem::path(std::wstring(buffer, length)).parent_path().u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
  return ".";
#endif
}

std::filesystem::path exe_directory() {
  return file::from_utf8(exe_directory_utf8());
}

std::string normalize_resource(std::string name) {
  const size_t colon = name.find(':');
  if (colon != std::string::npos) name = name.substr(colon + 1);
  return name;
}

std::string strip_namespace(std::string_view name) {
  const size_t colon = name.find(':');
  if (colon == std::string_view::npos) return std::string(name);
  return std::string(name.substr(colon + 1));
}

void log_info(std::string_view message) { reim::log::info("convert", message); }
void log_warn(std::string_view message) { reim::log::warn("convert", message); }
void log_error(std::string_view message) { reim::log::error("convert", message); }

} // namespace reim::conv
