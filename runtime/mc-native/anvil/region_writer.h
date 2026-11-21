#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reim::anvil {

/*
 * M3-B1/A7 region writer spike.
 *
 * Replaces one chunk payload in an existing Anvil region (or creates the region
 * file), always compressing with zlib (type 2). Existing chunks, including
 * external .mcc chunks, are copied verbatim; the target chunk's old sectors are
 * dropped and new sectors appended. The file is written to a temp path,
 * flushed, then atomically replaced, so an interrupted write cannot leave a
 * truncated region behind.
 *
 * Not yet covered here (B1/B5): heightmap maintenance, sector reclamation for
 * long sessions, journal/marker recovery, LZ4 writing.
 */
bool write_chunk(const std::filesystem::path& region_path, int local_x,
                 int local_z, const std::vector<uint8_t>& nbt,
                 std::string& error);

} // namespace reim::anvil
