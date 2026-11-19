#include "anvil/region.h"

#include "util/compression.h"
#include "util/file.h"

#include <algorithm>
#include <charconv>
#include <cstring>

namespace reim::anvil {
namespace {

constexpr size_t kSectorSize = 4096;
constexpr size_t kHeaderSize = kSectorSize * 2;

uint32_t read_be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

int32_t read_be_i32(const uint8_t* p) {
  return static_cast<int32_t>(read_be32(p));
}

std::string chunk_label(int region_x, int region_z, int local_x, int local_z) {
  return "r." + std::to_string(region_x) + "." + std::to_string(region_z) + " chunk " +
         std::to_string(local_x) + "," + std::to_string(local_z);
}

} // namespace

bool parse_region_coords(const std::filesystem::path& path, int& region_x,
                         int& region_z) {
  const std::string name = path.filename().string();
  // Expected: r.<x>.<z> [ .mca ]
  if (name.size() < 5 || name[0] != 'r' || name[1] != '.') return false;
  size_t dot = name.find('.', 2);
  if (dot == std::string::npos) return false;
  int x = 0;
  auto result = std::from_chars(name.data() + 2, name.data() + dot, x);
  if (result.ec != std::errc{} || result.ptr != name.data() + dot) return false;
  const size_t second = name.find('.', dot + 1);
  if (second == std::string::npos) return false;
  int z = 0;
  result = std::from_chars(name.data() + dot + 1, name.data() + second, z);
  if (result.ec != std::errc{} || result.ptr != name.data() + second) return false;
  if (name.compare(second, std::string::npos, ".mca") != 0) return false;
  region_x = x;
  region_z = z;
  return true;
}

bool Region::open(const std::filesystem::path& path, std::string& error) {
  path_ = path;
  bytes_.clear();
  if (!parse_region_coords(path, region_x_, region_z_)) {
    error = "not an Anvil region file name: " + file::to_utf8(path);
    return false;
  }
  if (!file::read_all(path, bytes_, error)) return false;
  if (bytes_.size() < kHeaderSize) {
    error = "region file shorter than its 8 KiB header: " + file::to_utf8(path);
    bytes_.clear();
    return false;
  }
  return true;
}

std::vector<ChunkLocation> Region::present() const {
  std::vector<ChunkLocation> out;
  if (bytes_.size() < kHeaderSize) return out;
  for (int z = 0; z < 32; ++z) {
    for (int x = 0; x < 32; ++x) {
      const size_t index = static_cast<size_t>(z * 32 + x);
      const uint32_t word = read_be32(bytes_.data() + index * 4);
      if (word == 0) continue;
      ChunkLocation location;
      location.local_x = x;
      location.local_z = z;
      location.sector = word >> 8;
      location.sector_count = static_cast<uint8_t>(word & 0xFF);
      location.timestamp = read_be32(bytes_.data() + kSectorSize + index * 4);
      location.external = false;
      out.push_back(location);
    }
  }
  return out;
}

bool Region::has_chunk(int local_x, int local_z) const {
  if (local_x < 0 || local_x >= 32 || local_z < 0 || local_z >= 32) return false;
  if (bytes_.size() < kHeaderSize) return false;
  const size_t index = static_cast<size_t>(local_z * 32 + local_x);
  return read_be32(bytes_.data() + index * 4) != 0;
}

bool Region::read_chunk(int local_x, int local_z, std::vector<uint8_t>& out,
                        std::string& error) const {
  out.clear();
  if (local_x < 0 || local_x >= 32 || local_z < 0 || local_z >= 32) {
    error = "chunk coordinates outside region";
    return false;
  }
  if (bytes_.size() < kHeaderSize) {
    error = "region is not open";
    return false;
  }
  const size_t index = static_cast<size_t>(local_z * 32 + local_x);
  const uint32_t word = read_be32(bytes_.data() + index * 4);
  const std::string label = chunk_label(region_x_, region_z_, local_x, local_z);
  if (word == 0) {
    error = label + " is not present";
    return false;
  }
  const uint64_t sector = word >> 8;
  const uint64_t sector_count = word & 0xFF;
  if (sector_count == 0) {
    error = label + " has zero sectors";
    return false;
  }
  const uint64_t start = sector * kSectorSize;
  const uint64_t allocated = sector_count * kSectorSize;
  if (start + allocated > bytes_.size()) {
    error = label + " sectors run past the region file";
    return false;
  }
  if (allocated < 5) {
    error = label + " allocation is too small";
    return false;
  }
  const int32_t length = read_be_i32(bytes_.data() + start);
  if (length < 1 || static_cast<uint64_t>(length) + 4 > allocated) {
    error = label + " has an invalid payload length";
    return false;
  }
  const uint8_t raw_compression = bytes_[static_cast<size_t>(start + 4)];
  const uint8_t* payload = bytes_.data() + start + 5;
  const size_t payload_size = static_cast<size_t>(length) - 1;

  uint8_t compression = raw_compression;
  std::vector<uint8_t> external_data;
  if ((raw_compression & 0x80) != 0) {
    compression = static_cast<uint8_t>(raw_compression & 0x7F);
    const std::filesystem::path external_path =
        path_.parent_path() /
        ("c." + std::to_string(local_x) + "." + std::to_string(local_z) + ".mcc");
    if (!file::read_all(external_path, external_data, error)) {
      error = label + " external chunk: " + error;
      return false;
    }
    payload = external_data.data();
    // payload_size only covers the in-region header area for external chunks.
    (void)payload_size;
  }

  switch (compression) {
    case 1:
      if (!compression::gzip_decompress(
              payload, (raw_compression & 0x80) ? external_data.size() : payload_size,
              out, error)) {
        error = label + " gzip: " + error;
        return false;
      }
      return true;
    case 2:
      if (!compression::zlib_decompress(
              payload, (raw_compression & 0x80) ? external_data.size() : payload_size,
              out, error)) {
        error = label + " zlib: " + error;
        return false;
      }
      return true;
    case 3:
      out.assign(payload, payload + ((raw_compression & 0x80) ? external_data.size()
                                                               : payload_size));
      return true;
    case 4:
      error = label + " uses LZ4 compression, which M1 does not support";
      return false;
    case 127:
      error = label + " uses custom compression, which M1 does not support";
      return false;
    default:
      error = label + " has unknown compression type " +
              std::to_string(static_cast<int>(raw_compression));
      return false;
  }
}

} // namespace reim::anvil
