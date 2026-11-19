#include "util/zip.h"

#include "util/file.h"

#include <memory>

#include "miniz.h"

namespace reim::zip {

struct ZipHandle {
  mz_zip_archive archive{};
};

namespace {

std::string entry_name(mz_zip_archive* archive, mz_uint index) {
  char buffer[1024];
  buffer[0] = '\0';
  mz_zip_reader_get_filename(archive, index, buffer, sizeof(buffer));
  return std::string(buffer);
}

} // namespace

Archive::~Archive() { close(); }

bool Archive::open(const std::string& path_utf8, std::string& error) {
  close();
  file::Bytes bytes;
  if (!file::read_all(file::from_utf8(path_utf8), bytes, error)) return false;
  return open_memory(std::move(bytes), error);
}

bool Archive::open_memory(std::vector<uint8_t> bytes, std::string& error) {
  close();
  bytes_ = std::move(bytes);
  auto handle = std::make_unique<ZipHandle>();
  if (!mz_zip_reader_init_mem(&handle->archive, bytes_.data(), bytes_.size(), 0)) {
    error = "not a zip archive (miniz error)";
    return false;
  }
  zip_ = handle.release();
  return true;
}

void Archive::close() {
  if (zip_) {
    auto* handle = static_cast<ZipHandle*>(zip_);
    mz_zip_reader_end(&handle->archive);
    delete handle;
    zip_ = nullptr;
  }
  bytes_.clear();
}

size_t Archive::size() const {
  if (!zip_) return 0;
  auto* handle = static_cast<ZipHandle*>(zip_);
  return mz_zip_reader_get_num_files(&handle->archive);
}

bool Archive::contains(const std::string& name) const {
  if (!zip_) return false;
  auto* handle = static_cast<ZipHandle*>(zip_);
  return mz_zip_reader_locate_file(&handle->archive, name.c_str(), nullptr, 0) >= 0;
}

bool Archive::read(const std::string& name, std::vector<uint8_t>& out,
                   std::string& error) const {
  if (!zip_) {
    error = "archive is not open";
    return false;
  }
  auto* handle = static_cast<ZipHandle*>(zip_);
  const mz_int64 index = mz_zip_reader_locate_file(
      &handle->archive, name.c_str(), nullptr, 0);
  if (index < 0) {
    error = "zip entry not found: " + name;
    return false;
  }
  mz_zip_archive_file_stat stat{};
  if (!mz_zip_reader_file_stat(&handle->archive, static_cast<mz_uint>(index), &stat)) {
    error = "zip entry stat failed: " + name;
    return false;
  }
  out.resize(static_cast<size_t>(stat.m_uncomp_size));
  if (stat.m_uncomp_size > 0) {
    if (!mz_zip_reader_extract_to_mem(&handle->archive, static_cast<mz_uint>(index),
                                      out.data(), out.size(), 0)) {
      error = "zip entry extraction failed: " + name;
      return false;
    }
  }
  return true;
}

std::vector<std::string> Archive::list(const std::string& prefix) const {
  std::vector<std::string> names;
  if (!zip_) return names;
  auto* handle = static_cast<ZipHandle*>(zip_);
  const mz_uint count = mz_zip_reader_get_num_files(&handle->archive);
  names.reserve(count);
  for (mz_uint i = 0; i < count; ++i) {
    if (mz_zip_reader_is_file_a_directory(&handle->archive, i)) continue;
    std::string name = entry_name(&handle->archive, i);
    if (!prefix.empty() && name.rfind(prefix, 0) != 0) continue;
    names.push_back(std::move(name));
  }
  return names;
}

} // namespace reim::zip
