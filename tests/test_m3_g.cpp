/*
 * M3-G unit tests: deterministic world generation, the optional generated
 * worldgen profile, and the nether generator. Scenario coverage lives in
 * tests/scenarios/m3g (WLD-01..08).
 */
#include "catch.hpp"

#include "util/file.h"
#include "world/worldgen.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

uint64_t chunk_fingerprint(const reim::world::Chunk& chunk) {
  uint64_t hash = 1469598103934665603ull;
  auto mix = [&](uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
  };
  auto mix_text = [&](const std::string& text) {
    for (char c : text) mix(static_cast<unsigned char>(c));
  };
  mix(static_cast<uint64_t>(chunk.min_y));
  mix(static_cast<uint64_t>(chunk.max_y));
  mix(chunk.sections.size());
  for (const auto& section : chunk.sections) {
    mix(static_cast<uint64_t>(section.y));
    for (const auto& state : section.palette) mix_text(state.name);
    mix(section.indices.size());
    for (uint32_t index : section.indices) mix(index);
    for (const auto& biome : section.biome_palette) mix_text(biome);
    mix(section.biome_indices.size());
  }
  for (int x = chunk.x * 16; x < chunk.x * 16 + 16; x += 3) {
    for (int z = chunk.z * 16; z < chunk.z * 16 + 16; z += 3) {
      for (int y = chunk.min_y; y <= std::min(chunk.max_y, 120); y += 5) {
        mix_text(chunk.block(x, y, z).name);
        mix_text(chunk.biome(x, y, z));
      }
    }
  }
  return hash;
}

reim::world::WorldGenProfile profile(uint64_t seed) {
  reim::world::WorldGenProfile profile;
  profile.seed = seed;
  return profile;
}

} // namespace

TEST_CASE("M3-G overworld generation is deterministic per seed") {
  reim::world::WorldGenerator first;
  reim::world::WorldGenerator second;
  std::string error;
  REQUIRE(first.initialize(profile(12345), reim::world::GenDimension::overworld,
                           error));
  REQUIRE(second.initialize(profile(12345),
                            reim::world::GenDimension::overworld, error));
  reim::world::Chunk a;
  reim::world::Chunk b;
  reim::world::GenStats stats_a;
  reim::world::GenStats stats_b;
  first.generate_chunk(0, 0, a, stats_a);
  second.generate_chunk(0, 0, b, stats_b);
  CHECK(chunk_fingerprint(a) == chunk_fingerprint(b));
  CHECK(stats_a.ore_blocks == stats_b.ore_blocks);
  CHECK(stats_a.trees == stats_b.trees);
  CHECK(stats_a.plants == stats_b.plants);

  reim::world::WorldGenerator other;
  REQUIRE(other.initialize(profile(999), reim::world::GenDimension::overworld,
                           error));
  reim::world::Chunk c;
  reim::world::GenStats stats_c;
  other.generate_chunk(0, 0, c, stats_c);
  CHECK(chunk_fingerprint(a) != chunk_fingerprint(c));
}

TEST_CASE("M3-G overworld surface, biome and ores are in range") {
  reim::world::WorldGenerator generator;
  std::string error;
  REQUIRE(generator.initialize(profile(12345),
                               reim::world::GenDimension::overworld, error));
  for (int x = -32; x <= 32; x += 16) {
    for (int z = -32; z <= 32; z += 16) {
      const int surface = generator.surface_y(x, z);
      CHECK(surface > generator.profile().min_y + 4);
      CHECK(surface < generator.profile().max_y);
      CHECK_FALSE(generator.biome_at(x, z).empty());
    }
  }
  reim::world::Chunk chunk;
  reim::world::GenStats stats;
  generator.generate_chunk(0, 0, chunk, stats);
  CHECK(stats.chunks == 1);
  CHECK(stats.ore_blocks > 0);
  CHECK(stats.ores.count("minecraft:coal_ore") == 1);
  CHECK(stats.ores.count("minecraft:diamond_ore") == 1);
  CHECK(stats.trees + stats.plants > 0);
  CHECK(chunk.status == "full");
  CHECK(chunk.data_version == 5023);
  CHECK_FALSE(chunk.sections.empty());
}

TEST_CASE("M3-G nether generation uses its own range and biome") {
  reim::world::WorldGenerator generator;
  std::string error;
  REQUIRE(generator.initialize(profile(12345), reim::world::GenDimension::nether,
                               error));
  reim::world::Chunk chunk;
  reim::world::GenStats stats;
  generator.generate_chunk(0, 0, chunk, stats);
  CHECK(chunk.min_y == 0);
  CHECK(chunk.max_y == 127);
  CHECK(chunk.biome(0, 40, 0) == "minecraft:nether_wastes");
  bool saw_netherrack = false;
  bool saw_lava = false;
  for (int y = 0; y <= 127; ++y) {
    const std::string& name = chunk.block(0, y, 0).name;
    if (name == "minecraft:netherrack") saw_netherrack = true;
    if (name == "minecraft:lava") saw_lava = true;
  }
  CHECK(saw_netherrack);
  CHECK(saw_lava);
}

TEST_CASE("M3-G generated worldgen profile is validated") {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "reim-m3-worldgen-profile";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  REQUIRE(std::filesystem::create_directories(dir / "worldgen", ec));

  std::string error;
  reim::world::WorldGenProfile native;
  CHECK(native.load(dir, error));
  CHECK(native.source == "native");

  const std::string valid = R"({
    "format": 1, "sea_level": 70, "min_y": -64, "max_y": 319,
    "base_height": 70, "amplitude": 12, "tree_density": 0.0,
    "plant_density": 0.0
  })";
  REQUIRE(reim::file::write_text(dir / "worldgen" / "profile.json", valid, error));
  reim::world::WorldGenProfile loaded;
  CHECK(loaded.load(dir, error));
  CHECK(loaded.source == "data");
  CHECK(loaded.sea_level == 70);
  CHECK(loaded.amplitude == 12.0);

  const std::string invalid = R"({"format": 1, "sea_level": 9999})";
  REQUIRE(reim::file::write_text(dir / "worldgen" / "profile.json", invalid, error));
  reim::world::WorldGenProfile rejected;
  CHECK_FALSE(rejected.load(dir, error));
  CHECK_FALSE(error.empty());

  std::filesystem::remove_all(dir, ec);
}
