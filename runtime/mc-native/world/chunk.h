#pragma once

#include "nbt/nbt.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reim::world {

inline int floor_div(int value, int divisor) {
  const int quotient = value / divisor;
  return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1
                                                                   : quotient;
}

struct BlockState {
  std::string name;
  std::map<std::string, std::string, std::less<>> properties;

  bool operator==(const BlockState&) const = default;
};

struct Section {
  int y = 0;  // section coordinate: block y in [y*16, y*16+15]
  std::vector<BlockState> palette;
  std::vector<uint32_t> indices;  // 4096, empty when the palette has one entry
  std::vector<std::string> biome_palette;
  std::vector<uint32_t> biome_indices;  // 64 (4x4x4 cells), empty for one entry
  std::vector<uint8_t> sky_light;   // 2048 bytes = 4096 nibbles, empty if absent
  std::vector<uint8_t> block_light;

  const BlockState& block(int local_x, int local_y, int local_z) const;
  /* Empty when the section has no biome data. */
  std::string biome(int local_x, int local_y, int local_z) const;
  /* 0..15; 0 when the section carries no light layer. */
  uint8_t sky_light_at(int local_x, int local_y, int local_z) const;
  uint8_t block_light_at(int local_x, int local_y, int local_z) const;
  bool has_light() const { return !sky_light.empty() || !block_light.empty(); }
  bool non_empty() const;
};

/*
 * A single chunk column as stored in 26.3 Anvil data.
 *
 * Block palettes are polymorphic in 26.3: entries may be TAG_String
 * ("minecraft:tuff") or TAG_Compound either with {"": "minecraft:stone"} or
 * {"id": ..., "properties": {...}}. Legacy {Name, Properties} compounds are
 * accepted as well. Palettes with more than one entry are indexed by packed
 * big-endian longs, 4+ bits per entry, LSB-first, never crossing a word.
 */
struct Chunk {
  int x = 0;
  int z = 0;
  int data_version = 0;
  std::string status;
  int y_pos = 0;
  bool has_y_pos = false;
  int min_y = 0;  // lowest block y covered by this chunk
  int max_y = -1; // highest block y covered by this chunk
  std::size_t raw_section_count = 0;
  std::vector<Section> sections;  // sorted by section y

  const Section* section(int section_y) const;
  /* Air outside the stored world/sections. */
  const BlockState& block(int world_x, int y, int world_z) const;
  std::string biome(int world_x, int y, int world_z) const;
  uint8_t sky_light(int world_x, int y, int world_z) const;
  uint8_t block_light(int world_x, int y, int world_z) const;
  bool in_bounds(int y) const { return y >= min_y && y <= max_y; }
  std::size_t non_empty_sections() const;
  std::size_t palette_entries() const;
  std::size_t light_sections() const;
};

const BlockState& air_block();

/* `expected_x`/`expected_z` come from the region location; if the chunk NBT
 * carries xPos/zPos they must agree, otherwise the chunk is treated as
 * damaged rather than silently misplaced. */
bool parse_chunk(const nbt::Document& document, int expected_x, int expected_z,
                 Chunk& out, std::string& error);

} // namespace reim::world
