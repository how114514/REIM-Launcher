#include "anvil/region_writer.h"

#include "util/compression.h"
#include "util/file.h"

#include <algorithm>
#include <cstring>

namespace reim::anvil {
namespace {

constexpr size_t kSectorSize = 4096;
constexpr size_t kHeaderSize = kSectorSize * 2;

uint32_t read_be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void write_be32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}

} // namespace

bool write_chunk(const std::filesystem::path& region_path, int local_x,
                 int local_z, const std::vector<uint8_t>& nbt,
                 std::string& error) {
  if (local_x < 0 || local_x >= 32 || local_z < 0 || local_z >= 32) {
    error = "chunk coordinates outside region";
    return false;
  }
  if (nbt.empty()) {
    error = "refusing to write an empty chunk payload";
    return false;
  }
  std::vector<uint8_t> original;
  if (file::exists(region_path)) {
    if (!file::read_all(region_path, original, error)) return false;
  }
  if (original.size() < kHeaderSize) {
    original.assign(kHeaderSize, 0);
  }

  std::vector<uint8_t> compressed;
  if (!compression::zlib_compress(nbt.data(), nbt.size(), compressed, error)) {
    return false;
  }
  /* length = compression byte + compressed bytes */
  const size_t payload_bytes = 1 + compressed.size();
  const size_t sectors = (4 + payload_bytes + kSectorSize - 1) / kSectorSize;
  if (sectors == 0 || sectors > 255) {
    error = "chunk payload needs " + std::to_string(sectors) +
            " sectors (maximum 255)";
    return false;
  }

  const size_t target_index = static_cast<size_t>(local_z * 32 + local_x);
  const uint32_t target_word = read_be32(original.data() + target_index * 4);
  const uint32_t target_timestamp =
      read_be32(original.data() + kSectorSize + target_index * 4);
  const bool target_external = target_word != 0 &&
                               (original[static_cast<size_t>(target_word >> 8) *
                                             kSectorSize +
                                         4] &
                                0x80) != 0;

  std::vector<uint8_t> rebuilt(kHeaderSize, 0);
  size_t next_sector = 2;

  auto copy_chunk = [&](size_t index, uint32_t word, uint32_t timestamp) -> bool {
    const uint32_t sector = word >> 8;
    const uint32_t count = word & 0xFFu;
    if (count == 0) return false;
    const size_t start = static_cast<size_t>(sector) * kSectorSize;
    const size_t bytes = static_cast<size_t>(count) * kSectorSize;
    if (start + bytes > original.size()) return false;
    if (next_sector + count > 0xFFFFFFu) return false;
    rebuilt.resize(rebuilt.size() + bytes, 0);
    std::memcpy(rebuilt.data() + next_sector * kSectorSize,
                original.data() + start, bytes);
    const uint32_t new_word =
        (static_cast<uint32_t>(next_sector) << 8) | count;
    write_be32(rebuilt.data() + index * 4, new_word);
    write_be32(rebuilt.data() + kSectorSize + index * 4, timestamp);
    next_sector += count;
    return true;
  };

  for (size_t index = 0; index < 1024; ++index) {
    if (index == target_index) continue;
    const uint32_t word = read_be32(original.data() + index * 4);
    if (word == 0) continue;
    const uint32_t timestamp =
        read_be32(original.data() + kSectorSize + index * 4);
    if (!copy_chunk(index, word, timestamp)) {
      error = "existing chunk " + std::to_string(index) +
              " has an invalid sector allocation";
      return false;
    }
  }

  const size_t target_start = next_sector;
  const size_t total_sectors = sectors;
  rebuilt.resize(rebuilt.size() + total_sectors * kSectorSize, 0);
  uint8_t* target = rebuilt.data() + target_start * kSectorSize;
  write_be32(target, static_cast<uint32_t>(4 + payload_bytes));
  target[4] = 2; /* zlib */
  std::memcpy(target + 5, compressed.data(), compressed.size());
  write_be32(rebuilt.data() + target_index * 4,
             (static_cast<uint32_t>(target_start) << 8) |
                 static_cast<uint32_t>(sectors));
  write_be32(rebuilt.data() + kSectorSize + target_index * 4,
             target_timestamp);

  if (next_sector + sectors > 0xFFFFFFu) {
    error = "region would exceed the 24-bit sector address space";
    return false;
  }

  if (!file::write_atomic(region_path, rebuilt.data(), rebuilt.size(), error)) {
    return false;
  }
  if (target_external) {
    /* The target chunk is now inline; its external payload is obsolete. */
    std::error_code ec;
    const std::filesystem::path mcc =
        region_path.parent_path() /
        ("c." + std::to_string(local_x) + "." + std::to_string(local_z) + ".mcc");
    std::filesystem::remove(mcc, ec);
  }
  return true;
}

} // namespace reim::anvil
