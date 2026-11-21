#pragma once

#include "world/chunk.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace reim::world {

/*
 * Data-driven world generation (M3-G1..G3).
 *
 * The 26.3 noise settings / biome source / surface rules / features normally
 * come from the server jar's built-in datapack. That Converter ingestion is
 * M3-K5 (phase 4B); until it lands the Runtime accepts an optional
 * generated/26.3/worldgen/profile.json and otherwise uses the native M3
 * profile below. The source is reported in status so a native profile is
 * never presented as data-driven output (M3_PLAN 9.2/9.8).
 *
 * Generation is pure and deterministic: the same seed and chunk coordinates
 * always produce the same chunk. It runs on the generation worker, never on
 * the simulation thread.
 */
enum class GenDimension { overworld = 0, nether = 1, end = 2 };

const char* gen_dimension_name(GenDimension dimension);

struct GenStats {
  uint64_t chunks = 0;
  uint64_t ore_blocks = 0;
  uint64_t trees = 0;
  uint64_t plants = 0;
  std::map<std::string, uint64_t> ores;
};

struct WorldGenProfile {
  std::string source = "native";
  uint64_t seed = 0;
  int sea_level = 63;
  int min_y = -64;
  int max_y = 319;
  double base_height = 64.0;
  double amplitude = 20.0;
  double hill_amplitude = 7.0;
  double detail_amplitude = 2.0;
  double cave_threshold = 0.60;
  double tree_density = 0.055;
  double plant_density = 0.10;
  double coal_rate = 0.0040;
  double iron_rate = 0.0030;
  double copper_rate = 0.0025;
  double gold_rate = 0.0012;
  double redstone_rate = 0.0012;
  double lapis_rate = 0.0008;
  double diamond_rate = 0.0006;
  double emerald_rate = 0.0002;

  /* Optional generated/worldgen/profile.json. Missing file = native defaults;
   * a present but invalid file is an error (G1 validates what it loads). */
  bool load(const std::filesystem::path& generated_dir, std::string& error);
  bool validate(std::string& error) const;
};

class WorldGenerator {
 public:
  bool initialize(const WorldGenProfile& profile, GenDimension dimension,
                  std::string& error);

  const WorldGenProfile& profile() const { return profile_; }
  GenDimension dimension() const { return dimension_; }

  /* Deterministic generation of one chunk column. */
  void generate_chunk(int chunk_x, int chunk_z, Chunk& out, GenStats& stats) const;

  /* Terrain height before water, clamped to the dimension range. */
  int surface_y(int block_x, int block_z) const;
  std::string biome_at(int block_x, int block_z) const;

  /* First dry spawn column near (x, z), searching outwards deterministically. */
  bool find_spawn(int center_x, int center_z, int& out_x, int& out_y,
                  int& out_z) const;

 private:
  double height_at(int x, int z) const;
  double cave_noise(int x, int y, int z) const;
  void generate_overworld(int cx, int cz, Chunk& out, GenStats& stats) const;
  void generate_nether(int cx, int cz, Chunk& out, GenStats& stats) const;

  WorldGenProfile profile_;
  GenDimension dimension_ = GenDimension::overworld;
};

} // namespace reim::world
