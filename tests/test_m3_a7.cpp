/*
 * M3-A7 save-write spike.
 *
 * Runs ONLY on a disposable copy of a real save:
 *   REIM_TEST_WRITE_WORLD=<copy>  REIM_TEST_ALLOW_WRITE=1
 * The test refuses paths under .minecraft and any run without the explicit
 * allow flag. It exercises NBT write, gzip/zlib compression, one LZ4 read
 * block, an atomic region read-modify-write of one block, and a level.dat /
 * weather.dat write. Vanilla readability is NOT claimed here: no Java game
 * process is started (M3-A7 record marks it not_run).
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "catch.hpp"

#include "anvil/region.h"
#include "anvil/region_writer.h"
#include "nbt/nbt.h"
#include "util/compression.h"
#include "util/file.h"
#include "world/block_edit.h"
#include "world/chunk.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "lz4.h"

namespace {

bool write_world_available(std::filesystem::path& world) {
  const char* allow = std::getenv("REIM_TEST_ALLOW_WRITE");
  if (allow == nullptr || std::string(allow) != "1") return false;
  const char* env = std::getenv("REIM_TEST_WRITE_WORLD");
  if (env == nullptr || *env == '\0') return false;
  world = std::filesystem::path(env);
  if (!std::filesystem::is_directory(world)) return false;
  const std::string text = world.string();
  if (text.find(".minecraft") != std::string::npos) return false;
  return true;
}

} // namespace

TEST_CASE("M3-A7 NBT write is structurally stable") {
  std::filesystem::path world;
  if (!write_world_available(world)) {
    WARN("REIM_TEST_ALLOW_WRITE/REIM_TEST_WRITE_WORLD not set; skipping A7");
    return;
  }
  reim::nbt::Document document;
  std::string error;
  REQUIRE(reim::nbt::parse_file(world / "level.dat", document, error));
  std::vector<uint8_t> first;
  REQUIRE(reim::nbt::write(document, first, error));
  reim::nbt::Document reparsed;
  REQUIRE(reim::nbt::parse(first.data(), first.size(), reparsed, error));
  std::vector<uint8_t> second;
  REQUIRE(reim::nbt::write(reparsed, second, error));
  CHECK(first == second);

  std::vector<uint8_t> compressed;
  REQUIRE(reim::compression::gzip_compress(first.data(), first.size(), compressed,
                                           error));
  std::vector<uint8_t> inflated;
  REQUIRE(reim::compression::gzip_decompress(compressed.data(), compressed.size(),
                                             inflated, error));
  CHECK(inflated == first);

  std::vector<uint8_t> zlibbed;
  REQUIRE(reim::compression::zlib_compress(first.data(), first.size(), zlibbed,
                                           error));
  std::vector<uint8_t> unzlibbed;
  REQUIRE(reim::compression::zlib_decompress(zlibbed.data(), zlibbed.size(),
                                             unzlibbed, error));
  CHECK(unzlibbed == first);

  /* LZ4 region read path: one 4-byte little-endian length + LZ4 block. */
  std::vector<char> lz4(static_cast<size_t>(LZ4_compressBound(
      static_cast<int>(first.size()))));
  const int lz4_size =
      LZ4_compress_default(reinterpret_cast<const char*>(first.data()),
                           lz4.data(), static_cast<int>(first.size()),
                           static_cast<int>(lz4.size()));
  REQUIRE(lz4_size > 0);
  std::vector<uint8_t> region_format;
  region_format.push_back(static_cast<uint8_t>(lz4_size));
  region_format.push_back(static_cast<uint8_t>(lz4_size >> 8));
  region_format.push_back(static_cast<uint8_t>(lz4_size >> 16));
  region_format.push_back(static_cast<uint8_t>(lz4_size >> 24));
  region_format.insert(region_format.end(), lz4.begin(), lz4.begin() + lz4_size);
  std::vector<uint8_t> lz4_out;
  REQUIRE(reim::compression::lz4_decompress_region(
      region_format.data(), region_format.size(), lz4_out, error));
  CHECK(lz4_out == first);
}

TEST_CASE("M3-A7 region read-modify-write preserves the chunk") {
  std::filesystem::path world;
  if (!write_world_available(world)) {
    WARN("REIM_TEST_ALLOW_WRITE/REIM_TEST_WRITE_WORLD not set; skipping A7");
    return;
  }
  const std::filesystem::path region_path =
      world / "dimensions" / "minecraft" / "overworld" / "region" / "r.0.0.mca";
  REQUIRE(std::filesystem::is_regular_file(region_path));

  reim::anvil::Region region;
  std::string error;
  REQUIRE(region.open(region_path, error));
  REQUIRE(region.has_chunk(1, 5));
  std::vector<uint8_t> payload;
  REQUIRE(region.read_chunk(1, 5, payload, error));
  reim::nbt::Document document;
  REQUIRE(reim::nbt::parse_payload(payload, document, error));
  reim::world::Chunk before;
  REQUIRE(reim::world::parse_chunk(document, 1, 5, before, error));
  const auto& spawn_block = before.block(16, 64, 80);
  REQUIRE(spawn_block.name == "minecraft:grass_block");

  reim::world::BlockState stone;
  stone.name = "minecraft:stone";
  REQUIRE(reim::world::set_block(document, 16, 64, 80, stone, error));
  std::vector<uint8_t> written;
  INFO("NBT write error: " << error);
  REQUIRE(reim::nbt::write(document, written, error));
  REQUIRE(reim::anvil::write_chunk(region_path, 1, 5, written, error));

  reim::anvil::Region reopened;
  REQUIRE(reopened.open(region_path, error));
  REQUIRE(reopened.has_chunk(1, 5));
  std::vector<uint8_t> after_payload;
  REQUIRE(reopened.read_chunk(1, 5, after_payload, error));
  reim::nbt::Document after_document;
  REQUIRE(reim::nbt::parse_payload(after_payload, after_document, error));
  reim::world::Chunk after;
  REQUIRE(reim::world::parse_chunk(after_document, 1, 5, after, error));
  CHECK(after.block(16, 64, 80).name == "minecraft:stone");
  CHECK(after.data_version == 5023);
  CHECK(after.status == "minecraft:full");

  /* Unknown/section-adjacent fields survive. `entities` is present in 528 of
   * 529 audited chunks; this full chunk is the one without it, so it is not
   * required. The other lists are present in every chunk. */
  const reim::nbt::Value& root = after_document.root;
  CHECK(reim::nbt::find(root, "block_entities") != nullptr);
  CHECK(reim::nbt::find(root, "structures") != nullptr);
  CHECK(reim::nbt::find(root, "Heightmaps") != nullptr);
  CHECK(reim::nbt::find(root, "PostProcessing") != nullptr);
  CHECK(reim::nbt::find(root, "sections") != nullptr);

  /* Neighbouring chunks must be byte-semantically unchanged. */
  std::vector<uint8_t> neighbour_before;
  std::vector<uint8_t> neighbour_after;
  if (region.has_chunk(0, 5) && reopened.has_chunk(0, 5)) {
    REQUIRE(region.read_chunk(0, 5, neighbour_before, error));
    REQUIRE(reopened.read_chunk(0, 5, neighbour_after, error));
    CHECK(neighbour_before == neighbour_after);
  }
}

TEST_CASE("M3-A7 level.dat and split data write round-trip") {
  std::filesystem::path world;
  if (!write_world_available(world)) {
    WARN("REIM_TEST_ALLOW_WRITE/REIM_TEST_WRITE_WORLD not set; skipping A7");
    return;
  }
  std::string error;
  const std::filesystem::path level_path = world / "level.dat";
  reim::nbt::Document level;
  REQUIRE(reim::nbt::parse_file(level_path, level, error));
  std::vector<uint8_t> level_bytes;
  REQUIRE(reim::nbt::write(level, level_bytes, error));
  std::vector<uint8_t> compressed;
  REQUIRE(reim::compression::gzip_compress(level_bytes.data(), level_bytes.size(),
                                           compressed, error));
  REQUIRE(reim::file::write_atomic(level_path, compressed.data(), compressed.size(),
                                   error));
  reim::nbt::Document reread;
  REQUIRE(reim::nbt::parse_file(level_path, reread, error));
  std::vector<uint8_t> reread_bytes;
  REQUIRE(reim::nbt::write(reread, reread_bytes, error));
  CHECK(reread_bytes == level_bytes);

  const std::filesystem::path weather_path =
      world / "data" / "minecraft" / "weather.dat";
  reim::nbt::Document weather;
  REQUIRE(reim::nbt::parse_file(weather_path, weather, error));
  std::vector<uint8_t> weather_bytes;
  REQUIRE(reim::nbt::write(weather, weather_bytes, error));
  std::vector<uint8_t> weather_compressed;
  REQUIRE(reim::compression::gzip_compress(
      weather_bytes.data(), weather_bytes.size(), weather_compressed, error));
  REQUIRE(reim::file::write_atomic(weather_path, weather_compressed.data(),
                                   weather_compressed.size(), error));
  reim::nbt::Document weather_reread;
  REQUIRE(reim::nbt::parse_file(weather_path, weather_reread, error));
  std::vector<uint8_t> weather_reread_bytes;
  REQUIRE(reim::nbt::write(weather_reread, weather_reread_bytes, error));
  CHECK(weather_reread_bytes == weather_bytes);
}
