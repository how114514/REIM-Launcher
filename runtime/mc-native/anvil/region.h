#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reim::anvil {

/*
 * Read-only Anvil region reader (.mca).
 *
 * Header: 1024 32-bit big-endian location words (sector offset << 8 | sector
 * count), then 1024 timestamps. Chunk payloads begin with a 32-bit big-endian
 * length, one compression-type byte, then the compressed NBT:
 *   1 = gzip, 2 = zlib, 3 = uncompressed, 4 = LZ4 (unsupported in M1).
 * If the high bit (0x80) is set the chunk lives in `c.<localX>.<localZ>.mcc`
 * next to the region file.
 */

struct ChunkLocation {
  int local_x = 0;
  int local_z = 0;
  uint32_t sector = 0;
  uint8_t sector_count = 0;
  uint32_t timestamp = 0;
  bool external = false;
};

class Region {
 public:
  Region() = default;

  bool open(const std::filesystem::path& path, std::string& error);
  bool is_open() const { return !bytes_.empty(); }

  const std::filesystem::path& path() const { return path_; }
  int region_x() const { return region_x_; }
  int region_z() const { return region_z_; }

  /* Non-empty location entries, sorted by (local_x, local_z). */
  std::vector<ChunkLocation> present() const;
  bool has_chunk(int local_x, int local_z) const;

  /* Decompresses one chunk's NBT payload into `out`. Returns false with a
   * readable message for empty slots, truncated sectors, unsupported
   * compression or corrupt data. */
  bool read_chunk(int local_x, int local_z, std::vector<uint8_t>& out,
                  std::string& error) const;

 private:
  std::filesystem::path path_;
  std::vector<uint8_t> bytes_;
  int region_x_ = 0;
  int region_z_ = 0;
};

/* Parses `r.<x>.<z>.mca` (extension optional). */
bool parse_region_coords(const std::filesystem::path& path, int& region_x,
                         int& region_z);

} // namespace reim::anvil
