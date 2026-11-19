#include "world/chunk.h"

#include "samples/packed_bits.h"

#include <algorithm>
#include <optional>

namespace reim::world {
namespace {

constexpr size_t kBlocksPerSection = 4096;
constexpr size_t kBiomesPerSection = 64;

BlockState& air_storage() {
  static BlockState air = [] {
    BlockState value;
    value.name = "minecraft:air";
    return value;
  }();
  return air;
}

unsigned biome_bits(size_t palette_size) {
  unsigned bits = 1;
  while ((static_cast<size_t>(1) << bits) < palette_size) ++bits;
  return bits;
}

bool palette_properties(const nbt::Value& entry, BlockState& out, std::string& error) {
  if (out.name.empty()) {
    error = "block palette entry has no block id";
    return false;
  }
  const nbt::Value* properties = nbt::find(entry, "properties");
  if (properties == nullptr) properties = nbt::find(entry, "Properties");
  if (properties == nullptr) return true;
  if (properties->type != nbt::Tag::compound || !properties->compound) {
    error = "block palette entry properties is not a compound";
    return false;
  }
  for (const auto& [key, value] : *properties->compound) {
    if (const auto* text = nbt::as_string(value)) {
      out.properties.emplace(key, *text);
    } else if (const auto number = nbt::as_int(value)) {
      out.properties.emplace(key, std::to_string(*number));
    } else {
      error = "block property '" + key + "' is not a string or integer";
      return false;
    }
  }
  return true;
}

bool parse_block_palette_entry(const nbt::Value& entry, BlockState& out,
                               std::string& error) {
  out = BlockState{};
  if (entry.type == nbt::Tag::string) {
    out.name = entry.text;
    if (out.name.empty()) {
      error = "empty block name in palette";
      return false;
    }
    return true;
  }
  if (entry.type != nbt::Tag::compound || !entry.compound) {
    error = std::string("unsupported block palette entry type ") +
            nbt::tag_name(entry.type);
    return false;
  }
  // 26.3: {"": "minecraft:stone"} for stateless blocks.
  if (const nbt::Value* anonymous = nbt::find(entry, "")) {
    if (const auto* text = nbt::as_string(*anonymous)) {
      out.name = *text;
      return palette_properties(entry, out, error);
    }
  }
  // 26.3: {"id": ..., "properties": {...}}.
  if (const nbt::Value* id = nbt::find(entry, "id")) {
    if (const auto* text = nbt::as_string(*id)) {
      out.name = *text;
      return palette_properties(entry, out, error);
    }
  }
  // Pre-26.3 / vanilla classic form: {"Name": ..., "Properties": {...}}.
  if (const nbt::Value* name = nbt::find(entry, "Name")) {
    if (const auto* text = nbt::as_string(*name)) {
      out.name = *text;
      return palette_properties(entry, out, error);
    }
  }
  error = "block palette compound has no '' / id / Name entry";
  return false;
}

bool parse_biome_palette_entry(const nbt::Value& entry, std::string& out,
                               std::string& error) {
  if (const auto* text = nbt::as_string(entry)) {
    out = *text;
    return true;
  }
  if (entry.type == nbt::Tag::compound) {
    if (const nbt::Value* anonymous = nbt::find(entry, "")) {
      if (const auto* text = nbt::as_string(*anonymous)) {
        out = *text;
        return true;
      }
    }
    if (const nbt::Value* id = nbt::find(entry, "id")) {
      if (const auto* text = nbt::as_string(*id)) {
        out = *text;
        return true;
      }
    }
    if (const nbt::Value* name = nbt::find(entry, "Name")) {
      if (const auto* text = nbt::as_string(*name)) {
        out = *text;
        return true;
      }
    }
  }
  error = "unsupported biome palette entry";
  return false;
}

bool parse_block_states(const nbt::Value& section_value, Section& section,
                        std::string& error) {
  const nbt::Value* block_states = nbt::find(section_value, "block_states");
  if (block_states == nullptr) {
    section.palette.push_back(air_block());
    return true;
  }
  if (block_states->type != nbt::Tag::compound || !block_states->compound) {
    error = "block_states is not a compound";
    return false;
  }
  const nbt::Value* palette = nbt::find(*block_states, "palette");
  if (palette == nullptr || palette->type != nbt::Tag::list || !palette->list) {
    error = "block_states has no palette list";
    return false;
  }
  if (palette->list->items.empty()) {
    error = "block palette is empty";
    return false;
  }
  section.palette.reserve(palette->list->items.size());
  for (const nbt::Value& entry : palette->list->items) {
    BlockState state;
    if (!parse_block_palette_entry(entry, state, error)) return false;
    section.palette.push_back(std::move(state));
  }
  if (section.palette.size() == 1) return true;

  const nbt::Value* data = nbt::find(*block_states, "data");
  if (data == nullptr || data->type != nbt::Tag::long_array) {
    error = "block palette needs a data long array";
    return false;
  }
  const unsigned bits = samples::bits_for_palette(section.palette.size());
  if (!samples::unpack_packed(data->longs.data(), data->longs.size(), bits,
                              kBlocksPerSection, section.indices)) {
    error = "block data is too short for 4096 entries at " + std::to_string(bits) +
            " bits";
    return false;
  }
  for (uint32_t index : section.indices) {
    if (index >= section.palette.size()) {
      error = "block palette index out of range";
      return false;
    }
  }
  return true;
}

bool parse_light_layer(const nbt::Value& section_value, const char* key,
                       std::vector<uint8_t>& out, std::string& error) {
  const nbt::Value* layer = nbt::find(section_value, key);
  if (layer == nullptr) return true;
  if (layer->type != nbt::Tag::byte_array) {
    error = std::string(key) + " is not a byte array";
    return false;
  }
  if (layer->bytes.size() != 2048) {
    error = std::string(key) + " has " + std::to_string(layer->bytes.size()) +
            " bytes, expected 2048";
    return false;
  }
  out = layer->bytes;
  return true;
}

uint8_t light_nibble(const std::vector<uint8_t>& layer, int local_x, int local_y,
                     int local_z) {
  if (layer.size() < 2048) return 0;
  if (local_x < 0 || local_x > 15 || local_y < 0 || local_y > 15 || local_z < 0 ||
      local_z > 15) {
    return 0;
  }
  const size_t index =
      (static_cast<size_t>(local_y) << 8) | (static_cast<size_t>(local_z) << 4) |
      static_cast<size_t>(local_x);
  return static_cast<uint8_t>((layer[index >> 1] >> ((index & 1u) * 4)) & 0x0F);
}

bool parse_biomes(const nbt::Value& section_value, Section& section,
                  std::string& error) {
  const nbt::Value* biomes = nbt::find(section_value, "biomes");
  if (biomes == nullptr) return true;
  if (biomes->type != nbt::Tag::compound || !biomes->compound) {
    error = "biomes is not a compound";
    return false;
  }
  const nbt::Value* palette = nbt::find(*biomes, "palette");
  if (palette == nullptr || palette->type != nbt::Tag::list || !palette->list) {
    error = "biomes has no palette list";
    return false;
  }
  if (palette->list->items.empty()) {
    error = "biome palette is empty";
    return false;
  }
  section.biome_palette.reserve(palette->list->items.size());
  for (const nbt::Value& entry : palette->list->items) {
    std::string name;
    if (!parse_biome_palette_entry(entry, name, error)) return false;
    section.biome_palette.push_back(std::move(name));
  }
  if (section.biome_palette.size() == 1) return true;

  const nbt::Value* data = nbt::find(*biomes, "data");
  if (data == nullptr || data->type != nbt::Tag::long_array) {
    error = "biome palette needs a data long array";
    return false;
  }
  const unsigned bits = biome_bits(section.biome_palette.size());
  if (!samples::unpack_packed(data->longs.data(), data->longs.size(), bits,
                              kBiomesPerSection, section.biome_indices)) {
    error = "biome data is too short for 64 entries at " + std::to_string(bits) +
            " bits";
    return false;
  }
  for (uint32_t index : section.biome_indices) {
    if (index >= section.biome_palette.size()) {
      error = "biome palette index out of range";
      return false;
    }
  }
  return true;
}

} // namespace

const BlockState& air_block() { return air_storage(); }

const BlockState& Section::block(int local_x, int local_y, int local_z) const {
  if (local_x < 0 || local_x > 15 || local_y < 0 || local_y > 15 || local_z < 0 ||
      local_z > 15) {
    return air_block();
  }
  if (palette.empty()) return air_block();
  if (indices.empty()) return palette.front();
  const size_t index =
      (static_cast<size_t>(local_y) << 8) | (static_cast<size_t>(local_z) << 4) |
      static_cast<size_t>(local_x);
  if (index >= indices.size()) return air_block();
  const uint32_t palette_index = indices[index];
  if (palette_index >= palette.size()) return air_block();
  return palette[palette_index];
}

std::string Section::biome(int local_x, int local_y, int local_z) const {
  if (biome_palette.empty()) return {};
  if (biome_indices.empty()) return biome_palette.front();
  if (local_x < 0 || local_x > 15 || local_y < 0 || local_y > 15 || local_z < 0 ||
      local_z > 15) {
    return {};
  }
  const size_t cell_x = static_cast<size_t>(local_x) >> 2;
  const size_t cell_y = static_cast<size_t>(local_y) >> 2;
  const size_t cell_z = static_cast<size_t>(local_z) >> 2;
  const size_t index = (cell_y << 4) | (cell_z << 2) | cell_x;
  if (index >= biome_indices.size()) return {};
  const uint32_t palette_index = biome_indices[index];
  if (palette_index >= biome_palette.size()) return {};
  return biome_palette[palette_index];
}

uint8_t Section::sky_light_at(int local_x, int local_y, int local_z) const {
  return light_nibble(sky_light, local_x, local_y, local_z);
}

uint8_t Section::block_light_at(int local_x, int local_y, int local_z) const {
  return light_nibble(block_light, local_x, local_y, local_z);
}

bool Section::non_empty() const {
  for (const BlockState& state : palette) {
    if (!nbt::is_air_name(state.name)) return true;
  }
  return false;
}

const Section* Chunk::section(int section_y) const {
  // sections is sorted by y.
  const auto it = std::lower_bound(
      sections.begin(), sections.end(), section_y,
      [](const Section& section, int value) { return section.y < value; });
  if (it == sections.end() || it->y != section_y) return nullptr;
  return &*it;
}

const BlockState& Chunk::block(int world_x, int y, int world_z) const {
  const int section_y = floor_div(y, 16);
  const Section* found = section(section_y);
  if (found == nullptr) return air_block();
  const int local_x = world_x - x * 16;
  const int local_z = world_z - z * 16;
  const int local_y = y - section_y * 16;
  return found->block(local_x, local_y, local_z);
}

std::string Chunk::biome(int world_x, int y, int world_z) const {
  const int section_y = floor_div(y, 16);
  const Section* found = section(section_y);
  if (found == nullptr) return {};
  const int local_x = world_x - x * 16;
  const int local_z = world_z - z * 16;
  const int local_y = y - section_y * 16;
  return found->biome(local_x, local_y, local_z);
}

std::size_t Chunk::non_empty_sections() const {
  std::size_t count = 0;
  for (const Section& section_value : sections) {
    if (section_value.non_empty()) ++count;
  }
  return count;
}

std::size_t Chunk::palette_entries() const {
  std::size_t count = 0;
  for (const Section& section_value : sections) {
    count += section_value.palette.size();
  }
  return count;
}

uint8_t Chunk::sky_light(int world_x, int y, int world_z) const {
  const int section_y = floor_div(y, 16);
  const Section* found = section(section_y);
  if (found == nullptr) return 0;
  return found->sky_light_at(world_x - x * 16, y - section_y * 16, world_z - z * 16);
}

uint8_t Chunk::block_light(int world_x, int y, int world_z) const {
  const int section_y = floor_div(y, 16);
  const Section* found = section(section_y);
  if (found == nullptr) return 0;
  return found->block_light_at(world_x - x * 16, y - section_y * 16,
                               world_z - z * 16);
}

std::size_t Chunk::light_sections() const {
  std::size_t count = 0;
  for (const Section& section_value : sections) {
    if (section_value.has_light()) ++count;
  }
  return count;
}

bool parse_chunk(const nbt::Document& document, int expected_x, int expected_z,
                 Chunk& out, std::string& error) {
  out = Chunk{};
  out.x = expected_x;
  out.z = expected_z;
  if (document.root.type != nbt::Tag::compound || !document.root.compound) {
    error = "chunk root is not a compound";
    return false;
  }
  const nbt::Value& root = document.root;

  if (const auto value = [&]() -> std::optional<int64_t> {
        if (const nbt::Value* found = nbt::find(root, "xPos")) return nbt::as_int(*found);
        return std::nullopt;
      }()) {
    if (static_cast<int>(*value) != expected_x) {
      error = "chunk xPos does not match its region slot";
      return false;
    }
  }
  if (const auto value = [&]() -> std::optional<int64_t> {
        if (const nbt::Value* found = nbt::find(root, "zPos")) return nbt::as_int(*found);
        return std::nullopt;
      }()) {
    if (static_cast<int>(*value) != expected_z) {
      error = "chunk zPos does not match its region slot";
      return false;
    }
  }

  if (const nbt::Value* found = nbt::find(root, "DataVersion")) {
    if (const auto value = nbt::as_int(*found)) out.data_version = static_cast<int>(*value);
  }
  if (const nbt::Value* found = nbt::find(root, "Status")) {
    if (const auto* text = nbt::as_string(*found)) out.status = *text;
  }
  if (const nbt::Value* found = nbt::find(root, "yPos")) {
    if (const auto value = nbt::as_int(*found)) {
      out.y_pos = static_cast<int>(*value);
      out.has_y_pos = true;
    }
  }

  const nbt::Value* sections = nbt::find(root, "sections");
  if (sections == nullptr || sections->type != nbt::Tag::list || !sections->list) {
    // An empty chunk (no section list) has no terrain; bounds come from yPos
    // when available and stay empty otherwise.
    if (out.has_y_pos) {
      out.min_y = out.y_pos * 16;
      out.max_y = out.min_y - 1;
    }
    return true;
  }
  out.raw_section_count = sections->list->items.size();
  out.sections.reserve(sections->list->items.size());
  for (const nbt::Value& section_value : sections->list->items) {
    if (section_value.type != nbt::Tag::compound) {
      error = "section entry is not a compound";
      return false;
    }
    const nbt::Value* y_value = nbt::find(section_value, "Y");
    if (y_value == nullptr) {
      error = "section has no Y";
      return false;
    }
    const auto y = nbt::as_int(*y_value);
    if (!y) {
      error = "section Y is not an integer";
      return false;
    }
    Section section;
    section.y = static_cast<int>(*y);
    if (!parse_block_states(section_value, section, error)) {
      error = "section Y=" + std::to_string(section.y) + ": " + error;
      return false;
    }
    if (!parse_biomes(section_value, section, error)) {
      error = "section Y=" + std::to_string(section.y) + ": " + error;
      return false;
    }
    if (!parse_light_layer(section_value, "SkyLight", section.sky_light, error) ||
        !parse_light_layer(section_value, "BlockLight", section.block_light, error)) {
      error = "section Y=" + std::to_string(section.y) + ": " + error;
      return false;
    }
    out.sections.push_back(std::move(section));
  }
  std::sort(out.sections.begin(), out.sections.end(),
            [](const Section& a, const Section& b) { return a.y < b.y; });
  for (size_t i = 1; i < out.sections.size(); ++i) {
    if (out.sections[i - 1].y == out.sections[i].y) {
      error = "duplicate section Y=" + std::to_string(out.sections[i].y);
      return false;
    }
  }
  if (!out.sections.empty()) {
    out.min_y = out.sections.front().y * 16;
    out.max_y = out.sections.back().y * 16 + 15;
  } else if (out.has_y_pos) {
    out.min_y = out.y_pos * 16;
    out.max_y = out.min_y - 1;
  }
  return true;
}

} // namespace reim::world
