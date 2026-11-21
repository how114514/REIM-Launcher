#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace reim::conv {

using Json = nlohmann::json;

struct Args {
  std::vector<std::pair<std::string, std::string>> values;

  std::optional<std::string> get(std::string_view name) const {
    for (const auto& [key, value] : values) {
      if (key == name) return value;
    }
    return std::nullopt;
  }
  bool has(std::string_view name) const {
    for (const auto& [key, _] : values) {
      if (key == name) return true;
    }
    return false;
  }
};

Args parse_args(int argc, char** argv, int start_index);

Json parse_json_noexcept(const std::string& text);
Json parse_json_noexcept(std::string_view text);
bool read_json_file(const std::filesystem::path& path, Json& out, std::string& error);
bool write_json_file(const std::filesystem::path& path, const Json& value,
                     std::string& error);
bool write_text_file(const std::filesystem::path& path, const std::string& text,
                     std::string& error);

std::string exe_directory_utf8();
std::filesystem::path exe_directory();

std::string normalize_resource(std::string name);
std::string strip_namespace(std::string_view name);

/* Deterministic JSON writer: nlohmann's default object is std::map, so keys
 * are sorted; arrays preserve the order we build. */
inline std::string dump(const Json& value) { return value.dump(2) + "\n"; }

void log_info(std::string_view message);
void log_warn(std::string_view message);
void log_error(std::string_view message);

/* Runs an offline tool (JDK data generator / analyzer) and waits for it.
 * command_line is passed to CreateProcessW without a shell; the first token
 * must already be quoted by the caller. No Java process is ever started by
 * the Runtime; this is Converter-only. */
bool run_process_utf8(const std::string& application, const std::string& command_line,
                      int& exit_code, std::string& error);
std::string quote_process_arg(const std::string& value);

/* Rejects absolute paths, drive letters and ".." components. ZIP entry names
 * and resource-pack paths are untrusted input (M3-I4 security). */
bool safe_archive_path(const std::string& entry, std::string& error);

} // namespace reim::conv
