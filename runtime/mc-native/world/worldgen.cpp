#include "world/worldgen.h"

#include "util/file.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "json.hpp"

namespace reim::world {
namespace {

constexpr int kSectionAxis = 16;
constexpr uint32_t kBlocksPerSection = 4096;

uint64_t mix64(uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ull;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebull;
  value ^= value >> 31;
  return value;
}

uint64_t coord_hash(uint64_t seed, int x, int z) {
  return mix64(seed ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) ^
               static_cast<uint32_t>(z));
}

uint64_t coord_hash3(uint64_t seed, int x, int y, int z) {
  return mix64(seed ^
               (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 40) ^
               (static_cast<uint64_t>(static_cast<uint32_t>(z)) << 16) ^
               static_cast<uint32_t>(y));
}

double hash01(uint64_t h) {
  return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

double smooth(double t) { return t * t * (3.0 - 2.0 * t); }

double value_noise2(uint64_t seed, double x, double z) {
  const int x0 = static_cast<int>(std::floor(x));
  const int z0 = static_cast<int>(std::floor(z));
  const double fx = smooth(x - x0);
  const double fz = smooth(z - z0);
  const double a = hash01(coord_hash(seed, x0, z0));
  const double b = hash01(coord_hash(seed, x0 + 1, z0));
  const double c = hash01(coord_hash(seed, x0, z0 + 1));
  const double d = hash01(coord_hash(seed, x0 + 1, z0 + 1));
  return (a * (1.0 - fx) + b * fx) * (1.0 - fz) +
         (c * (1.0 - fx) + d * fx) * fz;
}

double value_noise3(uint64_t seed, double x, double y, double z) {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int z0 = static_cast<int>(std::floor(z));
  const double fx = smooth(x - x0);
  const double fy = smooth(y - y0);
  const double fz = smooth(z - z0);
  auto corner = [&](int dx, int dy, int dz) {
    return hash01(coord_hash3(seed, x0 + dx, y0 + dy, z0 + dz));
  };
  const double c00 = corner(0, 0, 0) * (1.0 - fx) + corner(1, 0, 0) * fx;
  const double c10 = corner(0, 1, 0) * (1.0 - fx) + corner(1, 1, 0) * fx;
  const double c01 = corner(0, 0, 1) * (1.0 - fx) + corner(1, 0, 1) * fx;
  const double c11 = corner(0, 1, 1) * (1.0 - fx) + corner(1, 1, 1) * fx;
  const double c0 = c00 * (1.0 - fy) + c10 * fy;
  const double c1 = c01 * (1.0 - fy) + c11 * fy;
  return c0 * (1.0 - fz) + c1 * fz;
}

double fbm2(uint64_t seed, double x, double z, int octaves) {
  double sum = 0.0;
  double amplitude = 1.0;
  double total = 0.0;
  for (int i = 0; i < octaves; ++i) {
    sum += value_noise2(seed + static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull,
                        x, z) *
           amplitude;
    total += amplitude;
    amplitude *= 0.5;
    x *= 2.0;
    z *= 2.0;
  }
  return total > 0.0 ? sum / total : 0.0;
}

double fbm3(uint64_t seed, double x, double y, double z, int octaves) {
  double sum = 0.0;
  double amplitude = 1.0;
  double total = 0.0;
  for (int i = 0; i < octaves; ++i) {
    sum += value_noise3(
               seed + static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull, x, y, z) *
           amplitude;
    total += amplitude;
    amplitude *= 0.5;
    x *= 2.0;
    y *= 2.0;
    z *= 2.0;
  }
  return total > 0.0 ? sum / total : 0.0;
}

struct SectionBuilder {
  Section section;
  std::vector<uint16_t> ids;
  std::vector<std::string> palette;
  std::unordered_map<std::string, uint16_t> index;
};

std::string_view bare_name(std::string_view name) {
  const size_t colon = name.find(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

int biome_cell_index(int local_x, int local_y, int local_z) {
  return ((local_y >> 2) << 4) | ((local_z >> 2) << 2) | (local_x >> 2);
}

} // namespace

const char* gen_dimension_name(GenDimension dimension) {
  switch (dimension) {
    case GenDimension::nether: return "minecraft:the_nether";
    case GenDimension::end: return "minecraft:the_end";
    case GenDimension::overworld:
    default: return "minecraft:overworld";
  }
}

bool WorldGenProfile::validate(std::string& error) const {
  if (sea_level <= min_y || sea_level >= max_y) {
    error = "worldgen sea_level outside the dimension height";
    return false;
  }
  if (max_y - min_y < 64) {
    error = "worldgen height range is too small";
    return false;
  }
  if (base_height <= static_cast<double>(min_y) ||
      base_height >= static_cast<double>(max_y)) {
    error = "worldgen base_height outside the dimension height";
    return false;
  }
  if (amplitude < 0.0 || hill_amplitude < 0.0 || detail_amplitude < 0.0) {
    error = "worldgen amplitudes must be non-negative";
    return false;
  }
  for (double density : {tree_density, plant_density}) {
    if (density < 0.0 || density > 1.0) {
      error = "worldgen feature density outside 0..1";
      return false;
    }
  }
  if (cave_threshold < 0.0 || cave_threshold > 1.0) {
    error = "worldgen cave_threshold outside 0..1";
    return false;
  }
  return true;
}

bool WorldGenProfile::load(const std::filesystem::path& generated_dir,
                           std::string& error) {
  const std::filesystem::path path = generated_dir / "worldgen" / "profile.json";
  if (!reim::file::exists(path)) {
    source = "native";
    return true;
  }
  std::string text;
  if (!reim::file::read_text(path, text, error)) return false;
  const auto json =
      nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    error = "worldgen profile is not a JSON object";
    return false;
  }
  if (json.value("format", 0) != 1) {
    error = "worldgen profile format is not 1";
    return false;
  }
  source = "data";
  sea_level = json.value("sea_level", sea_level);
  min_y = json.value("min_y", min_y);
  max_y = json.value("max_y", max_y);
  base_height = json.value("base_height", base_height);
  amplitude = json.value("amplitude", amplitude);
  hill_amplitude = json.value("hill_amplitude", hill_amplitude);
  detail_amplitude = json.value("detail_amplitude", detail_amplitude);
  cave_threshold = json.value("cave_threshold", cave_threshold);
  tree_density = json.value("tree_density", tree_density);
  plant_density = json.value("plant_density", plant_density);
  coal_rate = json.value("coal_rate", coal_rate);
  iron_rate = json.value("iron_rate", iron_rate);
  copper_rate = json.value("copper_rate", copper_rate);
  gold_rate = json.value("gold_rate", gold_rate);
  redstone_rate = json.value("redstone_rate", redstone_rate);
  lapis_rate = json.value("lapis_rate", lapis_rate);
  diamond_rate = json.value("diamond_rate", diamond_rate);
  emerald_rate = json.value("emerald_rate", emerald_rate);
  return validate(error);
}

bool WorldGenerator::initialize(const WorldGenProfile& profile,
                                GenDimension dimension, std::string& error) {
  profile_ = profile;
  dimension_ = dimension;
  if (!profile_.validate(error)) return false;
  return true;
}

double WorldGenerator::height_at(int x, int z) const {
  const uint64_t seed = profile_.seed;
  const double continental =
      fbm2(seed ^ 0xA0761D6478BD642Full, x / 230.0, z / 230.0, 4);
  const double hills =
      fbm2(seed ^ 0xE7037ED1A0B428DBull, x / 72.0, z / 72.0, 3);
  const double detail =
      fbm2(seed ^ 0x8EBC6AF09C88C6E3ull, x / 18.0, z / 18.0, 2);
  double height = profile_.base_height +
                  (continental * 2.0 - 1.0) * profile_.amplitude +
                  (hills * 2.0 - 1.0) * profile_.hill_amplitude +
                  (detail * 2.0 - 1.0) * profile_.detail_amplitude;
  height = std::clamp(height, static_cast<double>(profile_.min_y + 8),
                      static_cast<double>(profile_.min_y + 180));
  return height;
}

double WorldGenerator::cave_noise(int x, int y, int z) const {
  const double a = fbm3(profile_.seed ^ 0x5DEECE66Dull, x / 28.0, y / 18.0,
                        z / 28.0, 2);
  const double b = fbm3(profile_.seed ^ 0x2545F4914F6CDD1Dull, x / 40.0,
                        y / 24.0, z / 40.0, 2);
  return a * 0.6 + b * 0.4;
}

int WorldGenerator::surface_y(int x, int z) const {
  return static_cast<int>(std::floor(height_at(x, z)));
}

std::string WorldGenerator::biome_at(int x, int z) const {
  if (dimension_ == GenDimension::nether) return "minecraft:nether_wastes";
  if (dimension_ == GenDimension::end) return "minecraft:the_end";
  const double temperature =
      fbm2(profile_.seed ^ 0x123456789ABCDEFull, x / 320.0, z / 320.0, 3);
  const double humidity =
      fbm2(profile_.seed ^ 0xFEDCBA987654321ull, x / 280.0, z / 280.0, 3);
  if (temperature > 0.62 && humidity < 0.38) return "minecraft:desert";
  if (temperature < 0.32) return "minecraft:taiga";
  if (humidity > 0.58) return "minecraft:forest";
  return "minecraft:plains";
}

bool WorldGenerator::find_spawn(int center_x, int center_z, int& out_x,
                                int& out_y, int& out_z) const {
  for (int radius = 0; radius <= 64; radius += 8) {
    for (int dz = -radius; dz <= radius; dz += 8) {
      for (int dx = -radius; dx <= radius; dx += 8) {
        if (radius != 0 && std::abs(dx) != radius && std::abs(dz) != radius) {
          continue;
        }
        const int x = center_x + dx;
        const int z = center_z + dz;
        const std::string biome = biome_at(x, z);
        if (dimension_ == GenDimension::overworld && biome == "minecraft:ocean") {
          continue;
        }
        int y;
        if (dimension_ == GenDimension::nether) {
          y = static_cast<int>(48.0 + fbm2(profile_.seed ^ 0x99ull, x / 30.0,
                                           z / 30.0, 2) *
                                         8.0);
        } else {
          y = surface_y(x, z);
          if (y <= profile_.sea_level) continue;
        }
        out_x = x;
        out_y = y + 1;
        out_z = z;
        return true;
      }
    }
  }
  out_x = center_x;
  out_z = center_z;
  out_y = profile_.sea_level + 2;
  return false;
}

void WorldGenerator::generate_overworld(int cx, int cz, Chunk& out,
                                        GenStats& stats) const {
  const int min_section = floor_div(profile_.min_y, kSectionAxis);
  const int max_section = 6; /* terrain/features stay below y=112 */
  std::map<int, SectionBuilder> builders;
  for (int sec = min_section; sec <= max_section; ++sec) {
    SectionBuilder& builder = builders[sec];
    builder.section.y = sec;
    builder.ids.assign(kBlocksPerSection, 0);
    builder.palette.push_back("minecraft:air");
    builder.index.emplace("minecraft:air", 0);
  }

  auto set_block = [&](int wx, int y, int wz, std::string_view name) {
    const int sec = floor_div(y, kSectionAxis);
    auto found = builders.find(sec);
    if (found == builders.end()) return;
    SectionBuilder& builder = found->second;
    const int lx = wx - cx * kSectionAxis;
    const int lz = wz - cz * kSectionAxis;
    const int ly = y - sec * kSectionAxis;
    if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || ly < 0 || ly > 15) return;
    auto index = builder.index.find(std::string(name));
    if (index == builder.index.end()) {
      const uint16_t id = static_cast<uint16_t>(builder.palette.size());
      builder.palette.emplace_back(name);
      index = builder.index.emplace(std::string(name), id).first;
    }
    builder.ids[(static_cast<size_t>(ly) << 8) |
                (static_cast<size_t>(lz) << 4) | static_cast<size_t>(lx)] =
        index->second;
  };

  for (int lx = 0; lx < 16; ++lx) {
    for (int lz = 0; lz < 16; ++lz) {
      const int wx = cx * 16 + lx;
      const int wz = cz * 16 + lz;
      const int height = surface_y(wx, wz);
      const std::string biome = biome_at(wx, wz);
      const bool desert = biome == "minecraft:desert";
      const bool taiga = biome == "minecraft:taiga";
      const bool beach = height <= profile_.sea_level + 2;
      const std::string top =
          (desert || beach) ? "minecraft:sand" : "minecraft:grass_block";
      const std::string filler =
          (desert || beach) ? "minecraft:sand" : "minecraft:dirt";

      for (int y = profile_.min_y; y <= height; ++y) {
        std::string name = "minecraft:stone";
        if (y <= profile_.min_y + 2) {
          name = "minecraft:bedrock";
        } else if (y == height) {
          name = top;
          if (taiga) name = "minecraft:snow_block";
        } else if (y >= height - 3) {
          name = filler;
        }
        /* Caves: carve the solid column except the surface shell. */
        if (y > profile_.min_y + 4 && y < height - 3 &&
            cave_noise(wx, y, wz) > profile_.cave_threshold) {
          continue;
        }
        set_block(wx, y, wz, name);
      }

      if (height < profile_.sea_level) {
        for (int y = height + 1; y <= profile_.sea_level; ++y) {
          set_block(wx, y, wz, "minecraft:water");
        }
      }
    }
  }

  /* Ores: deterministic per-block chances by depth band (M3-G3). */
  struct OreBand {
    const char* name;
    double rate;
    int max_y;
    int min_y;
  };
  /* Deep/rare bands first: the per-block loop stops at the first match, so a
   * high-rate coal band would otherwise hide diamonds and redstone. */
  const OreBand bands[] = {
      {"minecraft:emerald_ore", profile_.emerald_rate, 160, 70},
      {"minecraft:diamond_ore", profile_.diamond_rate, 16, profile_.min_y + 4},
      {"minecraft:redstone_ore", profile_.redstone_rate, 16, profile_.min_y + 4},
      {"minecraft:lapis_ore", profile_.lapis_rate, 30, profile_.min_y + 4},
      {"minecraft:gold_ore", profile_.gold_rate, 32, profile_.min_y + 4},
      {"minecraft:iron_ore", profile_.iron_rate, 72, profile_.min_y + 4},
      {"minecraft:copper_ore", profile_.copper_rate, 96, profile_.min_y + 4},
      {"minecraft:coal_ore", profile_.coal_rate, 200, profile_.min_y + 4},
  };
  for (int lx = 0; lx < 16; ++lx) {
    for (int lz = 0; lz < 16; ++lz) {
      const int wx = cx * 16 + lx;
      const int wz = cz * 16 + lz;
      const int height = surface_y(wx, wz);
      for (int y = profile_.min_y + 3; y <= height - 4; ++y) {
        for (const OreBand& band : bands) {
          if (y < band.min_y || y > band.max_y) continue;
          const double roll =
              hash01(coord_hash3(profile_.seed ^ 0xABCDEF0123456789ull, wx, y,
                                 wz));
          if (roll < band.rate) {
            set_block(wx, y, wz, band.name);
            ++stats.ore_blocks;
            ++stats.ores[band.name];
            break;
          }
        }
      }
    }
  }

  /* Trees and plants (deterministic per column; kept inside the chunk so no
   * cross-chunk feature merging is needed yet). */
  for (int lx = 2; lx < 14; ++lx) {
    for (int lz = 2; lz < 14; ++lz) {
      const int wx = cx * 16 + lx;
      const int wz = cz * 16 + lz;
      const int height = surface_y(wx, wz);
      if (height <= profile_.sea_level + 1) continue;
      const std::string biome = biome_at(wx, wz);
      const bool tree_biome = biome == "minecraft:forest" ||
                              biome == "minecraft:plains" ||
                              biome == "minecraft:taiga";
      const double tree_roll =
          hash01(coord_hash(profile_.seed ^ 0x7777777777777777ull, wx, wz));
      if (tree_biome && tree_roll < profile_.tree_density) {
        const bool birch = biome == "minecraft:forest" &&
                           tree_roll < profile_.tree_density * 0.35;
        const bool spruce = biome == "minecraft:taiga";
        const char* log = birch ? "minecraft:birch_log"
                          : spruce ? "minecraft:spruce_log"
                                   : "minecraft:oak_log";
        const char* leaves = birch ? "minecraft:birch_leaves"
                             : spruce ? "minecraft:spruce_leaves"
                                      : "minecraft:oak_leaves";
        const int trunk = 4 + static_cast<int>(
                                  hash01(coord_hash3(profile_.seed ^ 0x3333ull,
                                                     wx, height, wz)) *
                                  3.0);
        for (int y = 1; y <= trunk; ++y) {
          set_block(wx, height + y, wz, log);
        }
        for (int dy = trunk - 2; dy <= trunk + 1; ++dy) {
          const int radius = dy >= trunk ? 1 : 2;
          for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
              if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
              if (dx == 0 && dz == 0 && dy <= trunk) continue;
              set_block(wx + dx, height + dy, wz + dz, leaves);
            }
          }
        }
        ++stats.trees;
        continue;
      }
      const double plant_roll =
          hash01(coord_hash(profile_.seed ^ 0x5555555555555555ull, wx, wz));
      if (plant_roll < profile_.plant_density) {
        const double pick =
            hash01(coord_hash3(profile_.seed ^ 0x9999ull, wx, height, wz));
        const char* plant = pick < 0.72 ? "minecraft:short_grass"
                            : pick < 0.86 ? "minecraft:poppy"
                                          : "minecraft:dandelion";
        set_block(wx, height + 1, wz, plant);
        ++stats.plants;
      }
    }
  }

  out.min_y = profile_.min_y;
  out.max_y = max_section * kSectionAxis + 15;
  out.y_pos = floor_div(out.min_y, kSectionAxis);
  out.has_y_pos = true;
  for (auto& [sec, builder] : builders) {
    Section& section = builder.section;
    section.palette.clear();
    for (const std::string& name : builder.palette) {
      BlockState state;
      state.name = name;
      section.palette.push_back(std::move(state));
    }
    if (section.palette.size() <= 1) {
      section.indices.clear();
    } else {
      section.indices.assign(kBlocksPerSection, 0);
      for (size_t i = 0; i < kBlocksPerSection; ++i) {
        section.indices[i] = builder.ids[i];
      }
    }
    section.biome_indices.assign(64, 0);
    std::map<std::string, uint16_t> biome_index;
    for (int ly = 0; ly < 16; ly += 4) {
      for (int lz = 0; lz < 16; lz += 4) {
        for (int lx = 0; lx < 16; lx += 4) {
          const int wx = cx * 16 + lx + 2;
          const int wz = cz * 16 + lz + 2;
          const std::string biome = biome_at(wx, wz);
          auto found = biome_index.find(biome);
          if (found == biome_index.end()) {
            const uint16_t id = static_cast<uint16_t>(section.biome_palette.size());
            section.biome_palette.push_back(biome);
            found = biome_index.emplace(biome, id).first;
          }
          section.biome_indices[static_cast<size_t>(biome_cell_index(lx, ly, lz))] =
              found->second;
        }
      }
    }
    section.biome_indices.resize(64);
    out.sections.push_back(std::move(section));
    (void)sec;
  }
}

void WorldGenerator::generate_nether(int cx, int cz, Chunk& out,
                                     GenStats& stats) const {
  const int min_section = 0;
  const int max_section = 7;
  std::map<int, SectionBuilder> builders;
  for (int sec = min_section; sec <= max_section; ++sec) {
    SectionBuilder& builder = builders[sec];
    builder.section.y = sec;
    builder.ids.assign(kBlocksPerSection, 0);
    builder.palette.push_back("minecraft:air");
    builder.index.emplace("minecraft:air", 0);
  }
  auto set_block = [&](int wx, int y, int wz, std::string_view name) {
    const int sec = floor_div(y, kSectionAxis);
    auto found = builders.find(sec);
    if (found == builders.end()) return;
    SectionBuilder& builder = found->second;
    const int lx = wx - cx * kSectionAxis;
    const int lz = wz - cz * kSectionAxis;
    const int ly = y - sec * kSectionAxis;
    if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || ly < 0 || ly > 15) return;
    auto index = builder.index.find(std::string(name));
    if (index == builder.index.end()) {
      const uint16_t id = static_cast<uint16_t>(builder.palette.size());
      builder.palette.emplace_back(name);
      index = builder.index.emplace(std::string(name), id).first;
    }
    builder.ids[(static_cast<size_t>(ly) << 8) |
                (static_cast<size_t>(lz) << 4) | static_cast<size_t>(lx)] =
        index->second;
  };

  for (int lx = 0; lx < 16; ++lx) {
    for (int lz = 0; lz < 16; ++lz) {
      const int wx = cx * 16 + lx;
      const int wz = cz * 16 + lz;
      const int floor_height =
          30 + static_cast<int>(fbm2(profile_.seed ^ 0x1234ull, wx / 42.0,
                                     wz / 42.0, 3) *
                                20.0);
      const int ceiling =
          96 + static_cast<int>(fbm2(profile_.seed ^ 0x4321ull, wx / 36.0,
                                     wz / 36.0, 3) *
                                12.0);
      for (int y = 0; y <= floor_height; ++y) {
        std::string name = "minecraft:netherrack";
        if (y <= 2) name = "minecraft:bedrock";
        else if (y <= 31 && hash01(coord_hash3(profile_.seed ^ 0x51ull, wx, y,
                                               wz)) < 0.7) {
          name = "minecraft:lava";
        } else if (y == floor_height &&
                   hash01(coord_hash3(profile_.seed ^ 0x52ull, wx, y, wz)) <
                       0.08) {
          name = "minecraft:soul_sand";
        } else if (hash01(coord_hash3(profile_.seed ^ 0x53ull, wx, y, wz)) <
                   0.03) {
          name = "minecraft:blackstone";
        }
        set_block(wx, y, wz, name);
      }
      for (int y = floor_height + 1; y <= 31; ++y) {
        set_block(wx, y, wz, "minecraft:lava");
      }
      for (int y = ceiling; y <= 127; ++y) {
        set_block(wx, y, wz, "minecraft:netherrack");
      }
      if (hash01(coord_hash(profile_.seed ^ 0x61ull, wx, wz)) < 0.02) {
        set_block(wx, ceiling - 1, wz, "minecraft:glowstone");
      }
    }
  }

  out.min_y = 0;
  out.max_y = 127;
  out.y_pos = 0;
  out.has_y_pos = true;
  for (auto& [sec, builder] : builders) {
    Section& section = builder.section;
    section.palette.clear();
    for (const std::string& name : builder.palette) {
      BlockState state;
      state.name = name;
      section.palette.push_back(std::move(state));
    }
    if (section.palette.size() <= 1) {
      section.indices.clear();
    } else {
      section.indices.assign(kBlocksPerSection, 0);
      for (size_t i = 0; i < kBlocksPerSection; ++i) {
        section.indices[i] = builder.ids[i];
      }
    }
    section.biome_palette.push_back("minecraft:nether_wastes");
    section.biome_indices.clear();
    (void)stats;
    out.sections.push_back(std::move(section));
    (void)sec;
  }
}

void WorldGenerator::generate_chunk(int chunk_x, int chunk_z, Chunk& out,
                                    GenStats& stats) const {
  out = Chunk{};
  out.x = chunk_x;
  out.z = chunk_z;
  out.data_version = 5023;
  out.status = "full";
  if (dimension_ == GenDimension::nether) {
    generate_nether(chunk_x, chunk_z, out, stats);
  } else {
    generate_overworld(chunk_x, chunk_z, out, stats);
  }
  ++stats.chunks;
}

} // namespace reim::world
