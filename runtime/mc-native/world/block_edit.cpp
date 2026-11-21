#include "world/block_edit.h"

#include "samples/packed_bits.h"

#include <algorithm>
#include <memory>

namespace reim::world {
namespace {

constexpr size_t kBlocksPerSection = 4096;

nbt::Value make_string(std::string text) {
  nbt::Value value;
  value.type = nbt::Tag::string;
  value.text = std::move(text);
  return value;
}

nbt::Value make_palette_entry(const BlockState& state) {
  nbt::Value anonymous = make_string(state.name);
  nbt::Value entry;
  entry.type = nbt::Tag::compound;
  entry.compound = std::make_shared<nbt::Compound>();
  (*entry.compound)[""] = std::move(anonymous);
  if (!state.properties.empty()) {
    nbt::Value properties;
    properties.type = nbt::Tag::compound;
    properties.compound = std::make_shared<nbt::Compound>();
    for (const auto& [key, value] : state.properties) {
      (*properties.compound)[key] = make_string(value);
    }
    (*entry.compound)["properties"] = std::move(properties);
  }
  return entry;
}

bool entry_matches(const nbt::Value& entry, const BlockState& state) {
  std::string name;
  const nbt::Value* properties = nullptr;
  if (entry.type == nbt::Tag::string) {
    name = entry.text;
  } else if (entry.type == nbt::Tag::compound && entry.compound) {
    if (const auto* anonymous = nbt::find(entry, "")) {
      if (const auto* text = nbt::as_string(*anonymous)) name = *text;
    }
    if (name.empty()) {
      if (const auto* id = nbt::find(entry, "id")) {
        if (const auto* text = nbt::as_string(*id)) name = *text;
      }
    }
    if (name.empty()) {
      if (const auto* legacy = nbt::find(entry, "Name")) {
        if (const auto* text = nbt::as_string(*legacy)) name = *text;
      }
    }
    properties = nbt::find(entry, "properties");
    if (properties == nullptr) properties = nbt::find(entry, "Properties");
  }
  if (name != state.name) return false;
  if (state.properties.empty()) return properties == nullptr;
  if (properties == nullptr || properties->type != nbt::Tag::compound ||
      !properties->compound) {
    return false;
  }
  if (properties->compound->size() != state.properties.size()) return false;
  for (const auto& [key, expected] : state.properties) {
    const auto found = properties->compound->find(key);
    if (found == properties->compound->end()) return false;
    const auto* actual = nbt::as_string(found->second);
    if (actual == nullptr || *actual != expected) return false;
  }
  return true;
}

} // namespace

bool set_block(nbt::Document& document, int world_x, int y, int world_z,
               const BlockState& state, std::string& error) {
  if (document.root.type != nbt::Tag::compound || !document.root.compound) {
    error = "chunk root is not a compound";
    return false;
  }
  nbt::Value* sections = const_cast<nbt::Value*>(nbt::find(document.root, "sections"));
  if (sections == nullptr || sections->type != nbt::Tag::list || !sections->list) {
    error = "chunk has no section list";
    return false;
  }
  const int section_y = floor_div(y, 16);
  nbt::Value* section = nullptr;
  for (nbt::Value& candidate : sections->list->items) {
    if (candidate.type != nbt::Tag::compound || !candidate.compound) continue;
    const auto* y_value = nbt::find(candidate, "Y");
    if (y_value == nullptr) continue;
    const auto number = nbt::as_int(*y_value);
    if (number && static_cast<int>(*number) == section_y) {
      section = &candidate;
      break;
    }
  }
  if (section == nullptr) {
    error = "section Y=" + std::to_string(section_y) + " does not exist";
    return false;
  }

  nbt::Value* block_states = const_cast<nbt::Value*>(nbt::find(*section, "block_states"));
  if (block_states == nullptr) {
    block_states = &((*section->compound)["block_states"]);
    block_states->type = nbt::Tag::compound;
    block_states->compound = std::make_shared<nbt::Compound>();
  }
  if (block_states->type != nbt::Tag::compound || !block_states->compound) {
    error = "block_states is not a compound";
    return false;
  }
  nbt::Value* palette = const_cast<nbt::Value*>(nbt::find(*block_states, "palette"));
  if (palette == nullptr) {
    palette = &((*block_states->compound)["palette"]);
    palette->type = nbt::Tag::list;
    palette->list = std::make_shared<nbt::List>();
    palette->list->element_type = nbt::Tag::compound;
  }
  if (palette->type != nbt::Tag::list || !palette->list) {
    error = "block palette is not a list";
    return false;
  }
  /* Normalize the palette to compound entries. 26.3 writes compounds, but the
   * reader also accepts plain strings; a single NBT list cannot mix types. */
  palette->list->element_type = nbt::Tag::compound;
  for (nbt::Value& entry : palette->list->items) {
    if (entry.type != nbt::Tag::string) continue;
    nbt::Value wrapped;
    wrapped.type = nbt::Tag::compound;
    wrapped.compound = std::make_shared<nbt::Compound>();
    (*wrapped.compound)[""] = entry;
    entry = std::move(wrapped);
  }

  size_t palette_index = palette->list->items.size();
  for (size_t i = 0; i < palette->list->items.size(); ++i) {
    if (entry_matches(palette->list->items[i], state)) {
      palette_index = i;
      break;
    }
  }
  const size_t old_palette_size = palette->list->items.size();
  if (palette_index == old_palette_size) {
    palette->list->items.push_back(make_palette_entry(state));
  }

  const int local_x = world_x - floor_div(world_x, 16) * 16;
  const int local_z = world_z - floor_div(world_z, 16) * 16;
  const int local_y = y - section_y * 16;
  if (local_x < 0 || local_x > 15 || local_y < 0 || local_y > 15 ||
      local_z < 0 || local_z > 15) {
    error = "block coordinates outside the section";
    return false;
  }
  const size_t block_index = (static_cast<size_t>(local_y) << 8) |
                             (static_cast<size_t>(local_z) << 4) |
                             static_cast<size_t>(local_x);

  std::vector<uint32_t> indices;
  nbt::Value* data = const_cast<nbt::Value*>(nbt::find(*block_states, "data"));
  if (old_palette_size <= 1) {
    indices.assign(kBlocksPerSection, 0);
  } else {
    if (data == nullptr || data->type != nbt::Tag::long_array) {
      error = "palette has multiple entries but no data array";
      return false;
    }
    const unsigned old_bits = samples::bits_for_palette(old_palette_size);
    if (!samples::unpack_packed(data->longs.data(), data->longs.size(), old_bits,
                                kBlocksPerSection, indices)) {
      error = "block data is too short for 4096 entries";
      return false;
    }
  }
  indices[block_index] = static_cast<uint32_t>(palette_index);

  const size_t new_palette_size = palette->list->items.size();
  if (new_palette_size == 1) {
    if (data != nullptr) {
      block_states->compound->erase("data");
    }
    return true;
  }
  const unsigned new_bits = samples::bits_for_palette(new_palette_size);
  const std::vector<int64_t> packed =
      samples::pack_packed(indices.data(), indices.size(), new_bits);
  nbt::Value value;
  value.type = nbt::Tag::long_array;
  value.longs = packed;
  (*block_states->compound)["data"] = std::move(value);
  return true;
}

} // namespace reim::world
