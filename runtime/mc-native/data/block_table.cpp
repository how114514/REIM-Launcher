#include "data/block_table.h"

#include "util/file.h"

#include <map>
#include <string_view>

namespace reim::data {

const char* const kFaceNames[kFaceCount] = {"down", "up", "north",
                                            "south", "west", "east"};

namespace {

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

TintKind parse_tint_kind(const std::string& value) {
  if (value == "grass") return TintKind::grass;
  if (value == "foliage") return TintKind::foliage;
  if (value == "water") return TintKind::water;
  return TintKind::none;
}

int face_index(std::string_view name) {
  for (int i = 0; i < kFaceCount; ++i) {
    if (name == kFaceNames[i]) return i;
  }
  return -1;
}

/* blocks.json stores 0xAARRGGBB; the C ABI vertex tint is RGBA8 with red in
 * the low byte. */
uint32_t argb_to_rgba(uint32_t argb) {
  const uint32_t alpha = (argb >> 24) & 0xFFu;
  const uint32_t red = (argb >> 16) & 0xFFu;
  const uint32_t green = (argb >> 8) & 0xFFu;
  const uint32_t blue = argb & 0xFFu;
  return (alpha << 24) | (blue << 16) | (green << 8) | red;
}

std::string state_key(std::string_view name,
                      const std::map<std::string, std::string, std::less<>>& properties) {
  std::string key(name);
  key += '\x1f';
  bool first = true;
  for (const auto& [property, value] : properties) {
    if (!first) key += ',';
    first = false;
    key += property;
    key += '=';
    key += value;
  }
  return key;
}

} // namespace

void BlockTable::parse_visual(BlockDef& def, const Json& entry,
                              const std::map<std::string, FaceTexture>& sprites) {
  def.full_block = get_bool(entry, "full_block", def.full_block);
  def.collision = get_bool(entry, "collision", def.full_block);
  const Json* render = member(entry, "render_class");
  if (render != nullptr && render->is_string()) {
    def.render_class = parse_render_class(render->get<std::string>());
  }

  def.faces = {};
  const Json* face_sprites = member(entry, "sprites");
  const Json* face_tints = member(entry, "tints");
  const Json* face_kinds = member(entry, "tint_kinds");
  for (int face = 0; face < kFaceCount; ++face) {
    if (face_sprites != nullptr) {
      const Json* sprite = member(*face_sprites, kFaceNames[face]);
      if (sprite != nullptr && sprite->is_string()) {
        const auto found = sprites.find(sprite->get<std::string>());
        if (found != sprites.end()) def.faces[face] = found->second;
      }
    }
    if (face_tints != nullptr) {
      const Json* tint = member(*face_tints, kFaceNames[face]);
      if (tint != nullptr && tint->is_number()) {
        const uint32_t argb = get_u32(*face_tints, kFaceNames[face], 0xFFFFFFFFu);
        def.faces[face].tint = argb_to_rgba(argb);
      }
    }
    if (face_kinds != nullptr) {
      const Json* kind = member(*face_kinds, kFaceNames[face]);
      if (kind != nullptr && kind->is_string()) {
        def.faces[face].tint_kind = parse_tint_kind(kind->get<std::string>());
      }
    }
  }

  def.geometry.clear();
  const Json* geometry = member(entry, "geometry");
  if (geometry != nullptr && geometry->is_array()) {
    def.geometry.reserve(geometry->size());
    for (const Json& quad_json : *geometry) {
      if (!quad_json.is_object()) continue;
      const Json* sprite_name = member(quad_json, "sprite");
      if (sprite_name == nullptr || !sprite_name->is_string()) continue;
      const auto sprite = sprites.find(sprite_name->get<std::string>());
      if (sprite == sprites.end()) continue;
      const Json* corners = member(quad_json, "corners");
      const Json* uvs = member(quad_json, "uv");
      if (corners == nullptr || !corners->is_array() || corners->size() != 4 ||
          uvs == nullptr || !uvs->is_array() || uvs->size() != 4) {
        continue;
      }
      GeometryQuad quad;
      quad.sprite = sprite->second.sprite;
      quad.tint = sprite->second.tint;
      const float su0 = sprite->second.u0;
      const float sv0 = sprite->second.v0;
      const float su1 = sprite->second.u1;
      const float sv1 = sprite->second.v1;
      bool valid = true;
      for (int i = 0; i < 4 && valid; ++i) {
        const Json& corner = (*corners)[i];
        const Json& uv = (*uvs)[i];
        if (!corner.is_array() || corner.size() != 3 || !corner[0].is_number() ||
            !corner[1].is_number() || !corner[2].is_number()) {
          valid = false;
          break;
        }
        if (!uv.is_array() || uv.size() != 2 || !uv[0].is_number() ||
            !uv[1].is_number()) {
          valid = false;
          break;
        }
        for (int c = 0; c < 3; ++c) quad.corners[i][c] = corner[c].get<float>();
        // Geometry UVs are 0..16 texel coordinates within the sprite.
        const float u = uv[0].get<float>() / 16.0f;
        const float v = uv[1].get<float>() / 16.0f;
        quad.uv[i][0] = su0 + u * (su1 - su0);
        quad.uv[i][1] = sv0 + v * (sv1 - sv0);
      }
      if (!valid) continue;
      if (const Json* normal = member(quad_json, "normal");
          normal != nullptr && normal->is_array() && normal->size() == 3) {
        for (int c = 0; c < 3; ++c) {
          quad.normal[c] = (*normal)[c].is_number() ? (*normal)[c].get<float>() : 0.0f;
        }
      }
      if (const Json* kind = member(quad_json, "tint");
          kind != nullptr && kind->is_string()) {
        quad.tint_kind = parse_tint_kind(kind->get<std::string>());
      }
      if (const Json* cull = member(quad_json, "cull");
          cull != nullptr && cull->is_string()) {
        quad.cull_face = static_cast<int8_t>(face_index(cull->get<std::string>()));
      }
      def.geometry.push_back(quad);
    }
  }
}

bool BlockTable::load(const std::filesystem::path& generated_dir, std::string& error) {
  defs_.clear();
  by_name_.clear();
  by_state_.clear();
  biomes_.clear();

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
    def.liquid = get_bool(it.value(), "liquid");
    parse_visual(def, it.value(), sprite_table);
    by_name_.emplace(def.name, def.id);
    const uint32_t base_id = def.id;
    defs_.push_back(std::move(def));

    const Json* variants = member(it.value(), "variants");
    if (variants == nullptr || !variants->is_object()) continue;
    for (auto variant = variants->begin(); variant != variants->end(); ++variant) {
      if (!variant.value().is_object()) continue;
      BlockDef state = defs_[base_id];
      state.id = static_cast<uint32_t>(defs_.size());
      parse_visual(state, variant.value(), sprite_table);
      std::string key = state.name;
      key += '\x1f';
      key += variant.key();
      by_state_[key] = state.id;
      defs_.push_back(std::move(state));
    }
  }

  if (defs_.size() <= 1) {
    error = "blocks.json contains no block definitions";
    return false;
  }

  // Optional biome colour table (S4-5). Older generated data simply falls
  // back to the fixed per-face tints.
  if (file::exists(generated_dir / "biome_tints.json")) {
    std::string biome_text;
    if (file::read_text(generated_dir / "biome_tints.json", biome_text, error)) {
      const Json root = parse_noexcept(biome_text);
      const Json* biomes = root.is_object() ? member(root, "biomes") : nullptr;
      if (biomes != nullptr && biomes->is_object()) {
        for (auto it = biomes->begin(); it != biomes->end(); ++it) {
          if (!it.value().is_object()) continue;
          BiomeTint tint;
          tint.grass = argb_to_rgba(get_u32(it.value(), "grass", 0xFFFFFFFFu));
          tint.foliage = argb_to_rgba(get_u32(it.value(), "foliage", 0xFFFFFFFFu));
          tint.water = argb_to_rgba(get_u32(it.value(), "water", 0xFFFFFFFFu));
          biomes_.emplace(it.key(), tint);
        }
      }
    } else {
      error.clear();
    }
  }
  return true;
}

uint32_t BlockTable::find(std::string_view name) const {
  const auto it = by_name_.find(std::string(name));
  return it == by_name_.end() ? 0 : it->second;
}

uint32_t BlockTable::find_state(
    std::string_view name,
    const std::map<std::string, std::string, std::less<>>& properties) const {
  if (!properties.empty()) {
    const auto it = by_state_.find(state_key(name, properties));
    if (it != by_state_.end()) return it->second;
  }
  return find(name);
}

const BlockDef& BlockTable::def(uint32_t id) const {
  if (id >= defs_.size()) return defs_.front();
  return defs_[id];
}

bool BlockTable::biome_color(std::string_view biome, TintKind kind,
                             uint32_t& out) const {
  const auto it = biomes_.find(std::string(biome));
  if (it == biomes_.end()) return false;
  switch (kind) {
    case TintKind::grass:
      out = it->second.grass;
      return true;
    case TintKind::foliage:
      out = it->second.foliage;
      return true;
    case TintKind::water:
      out = it->second.water;
      return true;
    default:
      return false;
  }
}

} // namespace reim::data
