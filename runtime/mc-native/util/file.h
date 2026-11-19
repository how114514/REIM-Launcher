#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reim::file {

using Bytes = std::vector<uint8_t>;

bool exists(const std::filesystem::path& path);
bool is_directory(const std::filesystem::path& path);
bool read_all(const std::filesystem::path& path, Bytes& out, std::string& error);
bool read_text(const std::filesystem::path& path, std::string& out, std::string& error);
bool write_all(const std::filesystem::path& path, const void* data, size_t size,
               std::string& error);
bool write_text(const std::filesystem::path& path, const std::string& text,
                std::string& error);
bool ensure_directory(const std::filesystem::path& path, std::string& error);
std::string to_utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& path);
std::filesystem::path join(const std::filesystem::path& base, const char* child);

} // namespace reim::file
