#include "data/block_table.h"

#include "util/file.h"

#include <map>
#include <string_view>

#include "json.hpp"

namespace reim::data {

const char* const kFaceNames[kFaceCount] = {"down", "up", "north",
                                            "south", "west", "east"};

namespace {

using Json = nlohmann::json;

Json parse_noexcept(std::string_view text) {
  return Json::parse(text.begin(), text.end(), nullptr, false);
}

const Json* member(const Json& object, const char* name) {
  if (!object.is_object()) return nullptr;
  const auto it = object.find(name);
  return it == object.end() ? nullptr : &(*it);
}

bool get_bool(const Json& object, const char* name, bool fallback = false) {
  const Json* value = member(object, name);
  if (value == nullptr || !value->is_boolean()) return fallback;
  return value->get<bool>();
}

double get_number(const Json& object, const char* name, double fallback = 0.0) {
  const Json* value = member(object, name);
  if (value == nullptr || !value->is_number()) return fallback;
  return value->get<double>();
}

uint32_t get_u32(const Json& object, const char* name, uint32_t fallback = 0) {
  const Json* value = member(object, name);
  if (value == nullptr || !value->is_number()) return fallback;
  if (value->is_number_unsigned()) return value->get<uint32_t>();
  const int64_t signed_value = value->get<int64_t>();
  return signed_value < 0 ? fallback : static_cast<uint32_t>(signed_value);
}

RenderClass parse_render_class(const std::string& value) {
  if (value == "opaque") return RenderClass::opaque;
  if (value == "transparent") return RenderClass::transparent;
  return RenderClass::skip;
}

} // namespace

bool BlockTable::load(const std::filesystem::path& generated_dir, std::string& error) {
  defs_.clear();
  by_name_.clear();

  BlockDef air;
  air.id = 0;
  air.name = "minecraft:air";
  air.render_class = RenderClass::skip;
  defs_.push_back(std::move(air));
  by_name_.emplace("minecraft:air", 0);

  std::string text;
  if (!file::read_text(generated_dir / "textures.json", text, error)) return false;
  const Json textures_root = parse_noexcept(text);
  if (textures_root.is_discarded() || !textures_root.is_object()) {
    error = "textures.json is not valid JSON";
    return false;
  }
  const Json* sprites = member(textures_root, "sprites");
  if (sprites == nullptr || !sprites->is_object()) {
    error = "textures.json has no sprites object";
    return false;
  }
  std::map<std::string, FaceTexture> sprite_table;
  for (auto it = sprites->begin(); it != sprites->end(); ++it) {
    if (!it.value().is_object()) continue;
    FaceTexture info;
    const uint32_t index = get_u32(it.value(), "index", 0);
    info.sprite = static_cast<uint16_t>(index > 0xFFFFu ? 0xFFFFu : index);
    info.u0 = static_cast<float>(get_number(it.value(), "u0"));
    info.v0 = static_cast<float>(get_number(it.value(), "v0"));
    info.u1 = static_cast<float>(get_number(it.value(), "u1"));
    info.v1 = static_cast<float>(get_number(it.value(), "v1"));
    sprite_table.emplace(it.key(), info);
  }
  if (sprite_table.empty()) {
    error = "textures.json lists no sprites";
    return false;
  }

  if (!file::read_text(generated_dir / "blocks.json", text, error)) return false;
  const Json blocks_root = parse_noexcept(text);
  if (blocks_root.is_discarded() || !blocks_root.is_object()) {
    error = "blocks.json is not valid JSON";
    return false;
  }
  const Json* blocks = member(blocks_root, "blocks");
  if (blocks == nullptr || !blocks->is_object()) {
    error = "blocks.json has no blocks object";
    return false;
  }

  defs_.reserve(blocks->size() + 1);
  for (auto it = blocks->begin(); it != blocks->end(); ++it) {
    if (!it.value().is_object()) continue;
    BlockDef def;
    def.id = static_cast<uint32_t>(defs_.size());
    def.name = it.key();
    def.full_block = get_bool(it.value(), "full_block");
    def.collision = get_bool(it.value(), "collision");
    def.liquid = get_bool(it.value(), "liquid");
    const Json* render = member(it.value(), "render_class");
    def.render_class =
        parse_render_class(render && render->is_string() ? render->get<std::string>()
                                                         : std::string());
    const Json* face_sprites = member(it.value(), "sprites");
    const Json* face_tints = member(it.value(), "tints");
    for (int face = 0; face < kFaceCount; ++face) {
      if (face_sprites != nullptr) {
        const Json* sprite = member(*face_sprites, kFaceNames[face]);
        if (sprite != nullptr && sprite->is_string()) {
          const auto found = sprite_table.find(sprite->get<std::string>());
          if (found != sprite_table.end()) def.faces[face] = found->second;
        }
      }
      if (face_tints != nullptr) {
        const Json* tint = member(*face_tints, kFaceNames[face]);
        if (tint != nullptr && tint->is_number()) {
          // blocks.json stores 0xAARRGGBB (the Converter's tint form); the C
          // ABI vertex tint is RGBA8 with red in the low byte.
          const uint32_t argb = get_u32(*face_tints, kFaceNames[face], 0xFFFFFFFFu);
          const uint32_t alpha = (argb >> 24) & 0xFFu;
          const uint32_t red = (argb >> 16) & 0xFFu;
          const uint32_t green = (argb >> 8) & 0xFFu;
          const uint32_t blue = argb & 0xFFu;
          def.faces[face].tint = (alpha << 24) | (blue << 16) | (green << 8) | red;
        }
      }
    }
    by_name_.emplace(def.name, def.id);
    defs_.push_back(std::move(def));
  }

  if (defs_.size() <= 1) {
    error = "blocks.json contains no block definitions";
    return false;
  }
  return true;
}

uint32_t BlockTable::find(std::string_view name) const {
  const auto it = by_name_.find(std::string(name));
  return it == by_name_.end() ? 0 : it->second;
}

const BlockDef& BlockTable::def(uint32_t id) const {
  if (id >= defs_.size()) return defs_.front();
  return defs_[id];
}

} // namespace reim::data
