#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reim::zip {

class Archive {
 public:
  Archive() = default;
  ~Archive();
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;

  bool open(const std::string& path_utf8, std::string& error);
  bool open_memory(std::vector<uint8_t> bytes, std::string& error);
  void close();

  bool contains(const std::string& name) const;
  bool read(const std::string& name, std::vector<uint8_t>& out, std::string& error) const;
  std::vector<std::string> list(const std::string& prefix = {}) const;
  size_t size() const;

 private:
  std::vector<uint8_t> bytes_;
  void* zip_ = nullptr; // mz_zip_archive*
};

} // namespace reim::zip
