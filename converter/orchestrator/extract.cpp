#include "extract.h"

#include "reim_abi.h"

#include "model_resolver.h"
#include "prepare.h"
#include "util/file.h"
#include "util/hash.h"
#include "util/png.h"
#include "util/zip.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reim::conv {
namespace {

constexpr int kTileSize = 16;
constexpr int kAtlasWidth = 2048;
constexpr int kAtlasMaxHeight = 4096;

struct BlockStateInfo {
  std::string model;
  std::string representative_variant;
  std::map<std::string, std::set<std::string>> properties;
  /* canonical state key -> model reference, for every variant of the block. */
  std::map<std::string, std::string> variant_models;
  int variant_count = 0;
};

/* One resolved visual state (base block or a specific property variant). */
struct Visual {
  bool full_block = false;
  std::string render_class = "skip";
  Json sprites = Json::object();
  Json tints = Json::object();
  Json tint_kinds = Json::object();
  Json geometry = Json::array();
  std::string signature;
};

struct ExtractedBlock {
  std::string name;
  bool full_block = false;
  bool collision = false;
  bool liquid = false;
  std::string render_class = "skip";
  std::string lifecycle = "non_cube";
  Json properties = Json::object();
  Json sprites = Json::object();
  Json tints = Json::object();
  Json tint_kinds = Json::object();
  Json geometry = Json::array();
  std::map<std::string, Json> variants; // state key -> visual override object
  std::string model;
  int variant_count = 0;
  std::string representative_variant;
  std::vector<std::string> warnings;

  bool renderable() const { return render_class != "skip"; }
};

std::string block_name_from_entry(const std::string& entry) {
  constexpr const char* kPrefix = "assets/minecraft/blockstates/";
  std::string name = entry;
  if (name.rfind(kPrefix, 0) == 0) name = name.substr(std::string(kPrefix).size());
  if (name.size() >= 5 && name.substr(name.size() - 5) == ".json") {
    name.resize(name.size() - 5);
  }
  return "minecraft:" + name;
}

std::string join_properties(const std::map<std::string, std::set<std::string>>& properties) {
  std::string out;
  for (const auto& [key, values] : properties) {
    if (!out.empty()) out += ',';
    out += key;
    out += '=';
    out += values.empty() ? std::string() : *values.begin();
  }
  return out;
}

void collect_when(const Json& when,
                  std::map<std::string, std::set<std::string>>& properties) {
  if (when.is_array()) {
    for (const Json& item : when) collect_when(item, properties);
    return;
  }
  if (!when.is_object()) return;
  for (auto it = when.begin(); it != when.end(); ++it) {
    if (it.key() == "OR" || it.key() == "AND") {
      collect_when(it.value(), properties);
      continue;
    }
    if (it.value().is_array()) {
      for (const Json& item : it.value()) {
        if (item.is_string()) properties[it.key()].insert(item.get<std::string>());
        else if (item.is_boolean()) properties[it.key()].insert(item.get<bool>() ? "true" : "false");
      }
    } else if (it.value().is_string()) {
      properties[it.key()].insert(it.value().get<std::string>());
    } else if (it.value().is_boolean()) {
      properties[it.key()].insert(it.value().get<bool>() ? "true" : "false");
    }
  }
}

void collect_variant_key(const std::string& key,
                         std::map<std::string, std::set<std::string>>& properties) {
  if (key.empty()) return;
  size_t start = 0;
  while (start < key.size()) {
    const size_t comma = key.find(',', start);
    const std::string part = key.substr(start, comma == std::string::npos
                                                   ? std::string::npos
                                                   : comma - start);
    const size_t equals = part.find('=');
    if (equals != std::string::npos) {
      properties[part.substr(0, equals)].insert(part.substr(equals + 1));
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
}

/* Property strings are sorted the same way the Runtime builds a state key from
 * its std::map of palette properties. */
std::string canonical_state_key(const std::string& key) {
  std::map<std::string, std::string> pairs;
  size_t start = 0;
  while (start < key.size()) {
    const size_t comma = key.find(',', start);
    const std::string part = key.substr(start, comma == std::string::npos
                                                   ? std::string::npos
                                                   : comma - start);
    const size_t equals = part.find('=');
    if (equals != std::string::npos) {
      pairs[part.substr(0, equals)] = part.substr(equals + 1);
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  std::string out;
  for (const auto& [name, value] : pairs) {
    if (!out.empty()) out += ',';
    out += name;
    out += '=';
    out += value;
  }
  return out;
}

bool get_model_from_apply(const Json& apply, std::string& model) {
  if (apply.is_object()) {
    if (apply.contains("model") && apply["model"].is_string()) {
      model = apply["model"].get<std::string>();
      return true;
    }
    return false;
  }
  if (apply.is_array() && !apply.empty()) {
    return get_model_from_apply(apply[0], model);
  }
  return false;
}

bool is_default_property_value(const std::string& value) {
  static const std::set<std::string> kDefaults = {
      "false", "0", "y", "north", "up", "none", "normal", "straight",
      "floor", "single", "base", "bottom", "unpowered", "closed", "off",
      "empty", "intact", "east", "true"};
  return kDefaults.contains(value);
}

int variant_default_score(const std::string& key) {
  int score = 0;
  size_t start = 0;
  while (start < key.size()) {
    const size_t comma = key.find(',', start);
    const std::string part = key.substr(start, comma == std::string::npos
                                                   ? std::string::npos
                                                   : comma - start);
    const size_t equals = part.find('=');
    if (equals != std::string::npos &&
        !is_default_property_value(part.substr(equals + 1))) {
      ++score;
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return score;
}

bool parse_blockstate(const Json& root, BlockStateInfo& out, std::string& error) {
  out = BlockStateInfo{};
  if (!root.is_object()) {
    error = "blockstate root is not an object";
    return false;
  }
  if (root.contains("variants") && root["variants"].is_object()) {
    const Json& variants = root["variants"];
    out.variant_count = static_cast<int>(variants.size());
    int best_score = 1 << 30;
    for (auto it = variants.begin(); it != variants.end(); ++it) {
      collect_variant_key(it.key(), out.properties);
      std::string model;
      if (it.value().is_object()) {
        if (it.value().contains("model") && it.value()["model"].is_string()) {
          model = it.value()["model"].get<std::string>();
        }
      } else if (it.value().is_array() && !it.value().empty()) {
        get_model_from_apply(it.value(), model);
      }
      if (model.empty()) continue;
      out.variant_models.emplace(canonical_state_key(it.key()), model);
      const int score = variant_default_score(it.key());
      if (score < best_score) {
        best_score = score;
        out.model = model;
        out.representative_variant = it.key();
      }
    }
    if (out.model.empty()) {
      error = "no usable variant model";
      return false;
    }
    return true;
  }

  if (root.contains("multipart") && root["multipart"].is_array()) {
    const Json& parts = root["multipart"];
    out.variant_count = static_cast<int>(parts.size());
    bool selected = false;
    for (const Json& part : parts) {
      if (!part.is_object()) continue;
      if (part.contains("when")) collect_when(part["when"], out.properties);
      if (!selected && part.contains("apply")) {
        std::string model;
        if (get_model_from_apply(part["apply"], model)) {
          out.model = model;
          out.representative_variant = join_properties(out.properties);
          selected = true;
        }
      }
    }
    if (!selected) {
      error = "multipart blockstate has no apply model";
      return false;
    }
    return true;
  }

  error = "blockstate has neither variants nor multipart";
  return false;
}

bool is_liquid(const std::string& name) {
  return name == "minecraft:water" || name == "minecraft:lava" ||
         name == "minecraft:flowing_water" || name == "minecraft:flowing_lava";
}

bool is_air(const std::string& name) {
  return name == "minecraft:air" || name == "minecraft:cave_air" ||
         name == "minecraft:void_air";
}

bool is_transparent_cube(const std::string& name) {
  if (name == "minecraft:water" || name == "minecraft:glass" ||
      name == "minecraft:tinted_glass" || name == "minecraft:ice" ||
      name == "minecraft:frosted_ice" || name == "minecraft:slime_block" ||
      name == "minecraft:honey_block") {
    return true;
  }
  const std::string suffix = "_stained_glass";
  return name.size() >= suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Json properties_to_json(const std::map<std::string, std::set<std::string>>& properties) {
  Json out = Json::object();
  for (const auto& [key, values] : properties) {
    Json array = Json::array();
    for (const std::string& value : values) array.push_back(value);
    out[key] = array;
  }
  return out;
}

std::string sprite_value(const ExtractedBlock& block, int face_index) {
  const auto it = block.sprites.find(kFaceNames[face_index]);
  if (it == block.sprites.end() || !it->is_string()) return {};
  return it->get<std::string>();
}

void draw_missing_checker(std::vector<uint8_t>& atlas, int atlas_width, int x0, int y0) {
  for (int y = 0; y < kTileSize; ++y) {
    for (int x = 0; x < kTileSize; ++x) {
      const bool magenta = ((x / 4) + (y / 4)) % 2 == 0;
      const size_t index =
          (static_cast<size_t>(y0 + y) * atlas_width + (x0 + x)) * 4;
      atlas[index + 0] = magenta ? 255 : 0;
      atlas[index + 1] = 0;
      atlas[index + 2] = magenta ? 255 : 0;
      atlas[index + 3] = 255;
    }
  }
}

void blit_texture(std::vector<uint8_t>& atlas, int atlas_width, int x0, int y0,
                  const png::Image& image) {
  if (image.width <= 0 || image.height <= 0) return;
  const int frame_width = image.width;
  const int frame_height = std::min(image.height, image.width);
  for (int y = 0; y < kTileSize; ++y) {
    for (int x = 0; x < kTileSize; ++x) {
      const int sx = x * frame_width / kTileSize;
      const int sy = y * frame_height / kTileSize;
      const uint8_t* src = image.at(sx, sy);
      const size_t index = (static_cast<size_t>(y0 + y) * atlas_width + (x0 + x)) * 4;
      atlas[index + 0] = src[0];
      atlas[index + 1] = src[1];
      atlas[index + 2] = src[2];
      atlas[index + 3] = src[3];
    }
  }
}

int parse_hex_color(const Json& value, int fallback) {
  if (value.is_number_integer()) {
    const int64_t number = value.get<int64_t>();
    return number >= 0 && number <= 0xFFFFFFFF ? static_cast<int>(number) : fallback;
  }
  if (!value.is_string()) return fallback;
  const std::string text = value.get<std::string>();
  size_t index = 0;
  if (!text.empty() && text[0] == '#') index = 1;
  if (text.size() - index != 6) return fallback;
  int color = 0;
  for (size_t i = index; i < text.size(); ++i) {
    const char c = text[i];
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    if (digit < 0) return fallback;
    color = (color << 4) | digit;
  }
  return static_cast<int>(0xFF000000u | static_cast<uint32_t>(color));
}

/* Vanilla's triangular colormap lookup: temperature on the horizontal axis,
 * temperature*downfall on the vertical one. */
int colormap_color(const png::Image& map, double temperature, double downfall) {
  if (map.width <= 0 || map.height <= 0) return static_cast<int>(0xFFFFFFFFu);
  const double t = std::clamp(temperature, 0.0, 1.0);
  const double d = std::clamp(downfall, 0.0, 1.0) * t;
  int x = static_cast<int>((1.0 - t) * 255.0);
  int y = static_cast<int>((1.0 - d) * 255.0);
  x = std::clamp(x, 0, map.width - 1);
  y = std::clamp(y, 0, map.height - 1);
  const uint8_t* pixel = map.at(x, y);
  return static_cast<int>(0xFF000000u | (static_cast<uint32_t>(pixel[0]) << 16) |
                          (static_cast<uint32_t>(pixel[1]) << 8) | pixel[2]);
}

Json collect_biome_tints(const zip::Archive& archive,
                         const std::string& profile,
                         std::vector<std::string>& warnings) {
  Json grass_map;
  Json foliage_map;
  auto load_map = [&](const char* sprite, Json& out) {
    std::vector<uint8_t> bytes;
    std::string error;
    png::Image image;
    if (!archive.read(std::string("assets/minecraft/textures/colormap/") + sprite +
                          ".png",
                      bytes, error) ||
        !png::load_memory(bytes.data(), bytes.size(), 3, image, error)) {
      warnings.push_back(std::string("biome tint: missing colormap ") + sprite);
      return;
    }
    out = Json{{"width", image.width},
               {"height", image.height},
               {"pixels", image.pixels}};
  };
  load_map("grass", grass_map);
  load_map("foliage", foliage_map);

  auto sample = [&](const Json& map, double temperature, double downfall) {
    if (!map.is_object()) return static_cast<int>(0xFFFFFFFFu);
    png::Image image;
    image.width = map["width"].get<int>();
    image.height = map["height"].get<int>();
    image.channels = 3;
    image.pixels = map["pixels"].get<std::vector<uint8_t>>();
    return colormap_color(image, temperature, downfall);
  };

  Json biomes = Json::object();
  const auto entries = archive.list("data/minecraft/worldgen/biome/");
  for (const std::string& entry : entries) {
    if (entry.size() < 5 || entry.substr(entry.size() - 5) != ".json") continue;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!archive.read(entry, bytes, error)) continue;
    const Json root = parse_json_noexcept(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (root.is_discarded() || !root.is_object()) continue;

    std::string name = entry.substr(std::string("data/minecraft/worldgen/biome/").size());
    name.resize(name.size() - 5);
    const double temperature = root.contains("temperature") && root["temperature"].is_number()
                                   ? root["temperature"].get<double>()
                                   : 0.5;
    const double downfall = root.contains("downfall") && root["downfall"].is_number()
                                ? root["downfall"].get<double>()
                                : 0.5;
    const Json* effects =
        root.contains("effects") && root["effects"].is_object() ? &root["effects"] : nullptr;

    int grass = 0;
    if (effects != nullptr && effects->contains("grass_color")) {
      grass = parse_hex_color((*effects)["grass_color"], 0);
    }
    if (grass == 0) grass = sample(grass_map, temperature, downfall);
    int foliage = 0;
    if (effects != nullptr && effects->contains("foliage_color")) {
      foliage = parse_hex_color((*effects)["foliage_color"], 0);
    }
    if (foliage == 0) foliage = sample(foliage_map, temperature, downfall);
    int water = 0xFF3F76E4;
    if (effects != nullptr && effects->contains("water_color")) {
      water = parse_hex_color((*effects)["water_color"], 0xFF3F76E4);
    }
    biomes["minecraft:" + name] =
        Json{{"grass", static_cast<uint32_t>(grass)},
             {"foliage", static_cast<uint32_t>(foliage)},
             {"water", static_cast<uint32_t>(water)}};
  }

  Json out;
  out["schema"] = "reim.biome_tints";
  out["format"] = 2;
  out["profile"] = profile;
  out["source"] = "client.jar:data/minecraft/worldgen/biome + colormap";
  out["biome_count"] = biomes.size();
  out["biomes"] = biomes;
  return out;
}

const char* tint_kind_name(TintKind kind) {
  switch (kind) {
    case TintKind::grass: return "grass";
    case TintKind::foliage: return "foliage";
    case TintKind::water: return "water";
    default: return nullptr;
  }
}

Json quad_to_json(const ResolvedQuad& quad) {
  Json entry;
  entry["sprite"] = quad.sprite;
  Json corners = Json::array();
  for (const auto& corner : quad.corners) {
    corners.push_back(Json::array({corner[0], corner[1], corner[2]}));
  }
  entry["corners"] = corners;
  Json uvs = Json::array();
  for (const auto& uv : quad.uv) {
    uvs.push_back(Json::array({uv[0], uv[1]}));
  }
  entry["uv"] = uvs;
  entry["normal"] = Json::array({quad.normal[0], quad.normal[1], quad.normal[2]});
  if (quad.cull_face >= 0 && quad.cull_face < kFaceCount) {
    entry["cull"] = kFaceNames[static_cast<int>(quad.cull_face)];
  }
  if (const char* kind = tint_kind_name(quad.tint)) entry["tint"] = kind;
  return entry;
}

/* Resolve one model into the render data written to blocks.json. Full cubes
 * keep the compact per-face form; anything else becomes explicit quads. */
Visual make_visual(const ResolvedModel& model, const std::string& name) {
  Visual visual;
  if (!model.valid) return visual;
  if (model.full_cube) {
    visual.full_block = true;
    visual.render_class = is_transparent_cube(name) ? "transparent" : "opaque";
    for (int i = 0; i < kFaceCount; ++i) {
      visual.sprites[kFaceNames[i]] =
          model.face_sprites[i].empty() ? Json(nullptr) : Json(model.face_sprites[i]);
      visual.tints[kFaceNames[i]] = model.face_tints[i];
      if (const char* kind = tint_kind_name(model.face_tint_kinds[i])) {
        visual.tint_kinds[kFaceNames[i]] = kind;
      }
    }
  } else if (model.has_geometry) {
    visual.full_block = false;
    // Geometry writes depth; the UE/Preview opaque material is alpha-masked,
    // so leaves, plants and thin blocks share the opaque pass.
    visual.render_class = "opaque";
    for (const ResolvedQuad& quad : model.geometry) {
      if (quad.sprite.empty()) continue;
      visual.geometry.push_back(quad_to_json(quad));
    }
  }
  visual.signature = dump(visual.sprites) + dump(visual.tints) +
                     dump(visual.tint_kinds) + dump(visual.geometry) +
                     visual.render_class;
  return visual;
}

Json visual_to_json(const Visual& visual, bool liquid) {
  Json entry;
  entry["full_block"] = visual.full_block;
  entry["collision"] = visual.full_block && !liquid;
  entry["render_class"] = visual.render_class;
  entry["sprites"] = visual.sprites;
  entry["tints"] = visual.tints;
  entry["tint_kinds"] = visual.tint_kinds;
  if (!visual.geometry.empty()) entry["geometry"] = visual.geometry;
  return entry;
}

int next_power_of_two(int value) {
  int out = 16;
  while (out < value) out <<= 1;
  return out;
}

bool write_generated(const std::filesystem::path& out_dir, const std::string& profile,
                     const std::filesystem::path& profile_dir, const InputManifest& inputs,
                     const std::vector<ExtractedBlock>& blocks, size_t sprite_count,
                     int atlas_width, int atlas_height, const std::string& atlas_sha256,
                     const std::vector<std::string>& block_errors,
                     const std::vector<std::string>& missing_textures,
                     const std::vector<std::string>& model_warnings) {
  std::string error;

  // ---- blocks.json (schema v2) ------------------------------------------
  Json blocks_json;
  blocks_json["schema"] = "reim.blocks";
  blocks_json["format"] = 2;
  blocks_json["profile"] = profile;
  blocks_json["abi_version"] = REIM_ABI_VERSION;
  blocks_json["source"] = "client.jar:assets/minecraft/blockstates + models + textures";
  int full_blocks = 0;
  int renderable = 0;
  Json block_map = Json::object();
  for (const ExtractedBlock& block : blocks) {
    if (block.full_block) ++full_blocks;
    if (block.renderable()) ++renderable;
    Json entry;
    entry["full_block"] = block.full_block;
    entry["collision"] = block.collision;
    entry["liquid"] = block.liquid;
    entry["render_class"] = block.render_class;
    entry["material_class"] = block.render_class == "opaque"     ? "opaque"
                              : block.render_class == "transparent" ? "transparent"
                                                                   : "skip";
    entry["lifecycle"] = block.lifecycle;
    entry["properties"] = block.properties;
    entry["sprites"] = block.sprites;
    entry["tints"] = block.tints;
    if (!block.tint_kinds.empty()) entry["tint_kinds"] = block.tint_kinds;
    if (!block.geometry.empty()) entry["geometry"] = block.geometry;
    if (!block.variants.empty()) {
      Json variants = Json::object();
      for (const auto& [state_key, visual] : block.variants) {
        variants[state_key] = visual;
      }
      entry["variants"] = variants;
    }
    entry["model"] = block.model.empty() ? Json(nullptr) : Json(block.model);
    entry["variant_count"] = block.variant_count;
    entry["representative_variant"] = block.representative_variant;
    if (!block.warnings.empty()) {
      Json warnings = Json::array();
      for (const std::string& warning : block.warnings) warnings.push_back(warning);
      entry["warnings"] = warnings;
    }
    block_map[block.name] = entry;
  }
  blocks_json["block_count"] = blocks.size();
  blocks_json["full_block_count"] = full_blocks;
  blocks_json["renderable_count"] = renderable;
  blocks_json["blocks"] = block_map;
  const auto blocks_path = out_dir / "blocks.json";
  if (!write_json_file(blocks_path, blocks_json, error)) {
    log_error(error);
    return false;
  }

  // ---- extraction report (schema v2) ------------------------------------
  Json report;
  report["schema"] = "reim.extraction_report";
  report["format"] = 2;
  report["profile"] = profile;
  report["source"] = "client.jar:assets/minecraft/blockstates + models + textures";
  const auto reports_dir = profile_dir / "reports";
  const bool reports_available = file::is_directory(reports_dir);
  report["reports"] = Json{{"available", reports_available},
                           {"path", "reports"},
                           {"reason", reports_available
                                          ? "existing reports directory found"
                                          : "no reports directory in the profile input; "
                                            "the Minecraft data generator was not started "
                                            "(this environment forbids starting Minecraft/Java). "
                                            "Block tables are derived from the real client.jar "
                                            "assets instead; the reports gap is recorded here."}};
  report["counts"] = Json{{"blocks_total", blocks.size()},
                          {"blocks_full", full_blocks},
                          {"blocks_renderable", renderable},
                          {"sprites", sprite_count}};
  Json missing_json = Json::array();
  for (const std::string& value : missing_textures) missing_json.push_back(value);
  report["missing_textures"] = missing_json;
  Json errors_json = Json::array();
  for (const std::string& value : block_errors) errors_json.push_back(value);
  report["block_errors"] = errors_json;
  Json warnings_json = Json::array();
  for (const std::string& value : model_warnings) warnings_json.push_back(value);
  report["model_warnings"] = warnings_json;
  const auto report_path = out_dir / "extraction_report.json";
  if (!write_json_file(report_path, report, error)) {
    log_error(error);
    return false;
  }

  // ---- manifest (schema v2) ---------------------------------------------
  // Absolute input paths live only in debug_inputs.json (local diagnostics),
  // never in manifest.json, so the manifest hash is machine independent.
  Json manifest;
  manifest["schema"] = "reim.generated.manifest";
  manifest["format"] = 2;
  manifest["profile"] = profile;
  manifest["abi_version"] = REIM_ABI_VERSION;
  manifest["tool"] = "reim-convert";
  manifest["tool_version"] = "1.0.0-m2";
  manifest["capabilities"] = Json::array(
      {"frame_blob", "interpolation", "atlas_asset", "r0"});
  manifest["inputs"] = Json{
      {"client_jar_sha256", inputs.client_jar_sha256},
      {"client_jar_size", inputs.client_jar_size},
      {"version_json_sha256", inputs.version_json_sha256},
      {"version_json_size", inputs.version_json_size},
      {"asset_index_sha256", inputs.asset_index_sha256},
      {"asset_index_size", inputs.asset_index_size}};
  manifest["counts"] = Json{{"blocks_total", blocks.size()},
                            {"blocks_full", full_blocks},
                            {"blocks_renderable", renderable},
                            {"sprites", sprite_count},
                            {"missing_textures", missing_textures.size()}};
  manifest["atlas"] = Json{{"file", "atlas.png"},
                           {"width", atlas_width},
                           {"height", atlas_height},
                           {"tile_size", kTileSize},
                           {"padding", 0},
                           {"sampling", "nearest"},
                           {"format", "rgba8"},
                           {"mipmaps", false},
                           {"sha256", atlas_sha256},
                           {"meta_version", 2}};
  manifest["biomes"] = Json{{"file", "biome_tints.json"},
                            {"meta_version", 2},
                            {"tint_kinds", Json::array({"grass", "foliage", "water"})}};

  Json generated = Json::array();
  for (const char* name : {"atlas.png", "blocks.json", "textures.json",
                           "biome_tints.json", "extraction_report.json"}) {
    const auto path = out_dir / name;
    file::Bytes bytes;
    if (!file::read_all(path, bytes, error)) {
      log_error(error);
      return false;
    }
    Json entry;
    entry["path"] = name;
    entry["size"] = bytes.size();
    entry["sha256"] = hash::hex(hash::sha256(bytes.data(), bytes.size()));
    generated.push_back(entry);
  }
  manifest["generated"] = generated;
  const auto manifest_path = out_dir / "manifest.json";
  if (!write_json_file(manifest_path, manifest, error)) {
    log_error(error);
    return false;
  }

  Json debug_inputs;
  debug_inputs["note"] =
      "Local diagnostics only. Never part of manifest.json or any NativeVersion.";
  debug_inputs["client_jar"] = file::to_utf8(inputs.client_jar);
  debug_inputs["version_json"] = file::to_utf8(inputs.version_json);
  debug_inputs["asset_index"] = file::to_utf8(inputs.asset_index);
  debug_inputs["minecraft_dir"] = file::to_utf8(inputs.minecraft_dir);
  if (!write_json_file(out_dir / "debug_inputs.json", debug_inputs, error)) {
    log_error(error);
    return false;
  }

  log_info(std::format("extract: {} blocks ({} full cube, {} renderable), {} sprites",
                       blocks.size(), full_blocks, renderable, sprite_count));
  log_info(std::format("extract: wrote {}", file::to_utf8(out_dir)));
  return true;
}

} // namespace

int run_extract(const Args& args, const std::filesystem::path& exe_dir) {
  (void)exe_dir;
  const std::string profile = args.get("profile").value_or("26.3");
  const auto out_arg = args.get("out");
  if (!out_arg) {
    log_error("extract requires --profile 26.3 --out generated/26.3");
    return 2;
  }
  const auto out_dir = file::from_utf8(*out_arg);
  const auto profile_dir =
      args.get("profile-dir").has_value()
          ? file::from_utf8(*args.get("profile-dir"))
          : std::filesystem::path("work") / profile;

  std::string error;
  if (!file::ensure_directory(out_dir, error)) {
    log_error(error);
    return 3;
  }

  InputManifest inputs;
  const auto explicit_jar = args.get("jar");
  if (explicit_jar && !explicit_jar->empty()) {
    inputs.client_jar = file::from_utf8(*explicit_jar);
    inputs.profile = profile;
    if (!file::exists(inputs.client_jar)) {
      log_error("extract: client JAR not found: " + *explicit_jar);
      return 4;
    }
    file::Bytes bytes;
    if (!file::read_all(inputs.client_jar, bytes, error)) {
      log_error(error);
      return 4;
    }
    inputs.client_jar_size = bytes.size();
    inputs.client_jar_sha256 = hash::hex(hash::sha256(bytes.data(), bytes.size()));
  } else {
    if (!load_input_manifest(profile_dir, inputs, error)) {
      log_error("extract: " + error +
                " (run `reim-convert prepare` or pass --jar)");
      return 4;
    }
    if (inputs.profile != profile) {
      log_warn("extract: input manifest profile '" + inputs.profile +
               "' differs from requested '" + profile + "'");
    }
  }
  if (inputs.client_jar.empty() || !file::exists(inputs.client_jar)) {
    log_error("extract: client JAR not found");
    return 4;
  }

  zip::Archive archive;
  if (!archive.open(file::to_utf8(inputs.client_jar), error)) {
    log_error("extract: " + error);
    return 5;
  }

  std::vector<std::string> warnings;
  ModelResolver resolver(archive, warnings);
  std::vector<ExtractedBlock> blocks;
  std::vector<std::string> block_errors;
  std::vector<std::string> model_warnings;

  const auto blockstate_entries = archive.list("assets/minecraft/blockstates/");
  for (const std::string& entry : blockstate_entries) {
    if (entry.size() < 5 || entry.substr(entry.size() - 5) != ".json") continue;
    std::vector<uint8_t> bytes;
    if (!archive.read(entry, bytes, error)) {
      block_errors.push_back(entry + ": " + error);
      continue;
    }
    const Json root = parse_json_noexcept(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (root.is_discarded() || !root.is_object()) {
      block_errors.push_back(entry + ": invalid JSON");
      continue;
    }

    ExtractedBlock block;
    block.name = block_name_from_entry(entry);
    BlockStateInfo state_info;
    if (!parse_blockstate(root, state_info, error)) {
      block_errors.push_back(block.name + ": " + error);
      block.lifecycle = "unknown";
      blocks.push_back(std::move(block));
      continue;
    }
    block.model = state_info.model;
    block.variant_count = state_info.variant_count;
    block.representative_variant = state_info.representative_variant;
    block.properties = properties_to_json(state_info.properties);

    ResolvedModel model;
    if (!resolver.resolve(state_info.model, model, error)) {
      block_errors.push_back(block.name + ": " + error);
      block.lifecycle = "unknown";
      blocks.push_back(std::move(block));
      continue;
    }
    for (const std::string& warning : model.warnings) {
      block.warnings.push_back(warning);
      if (model_warnings.size() < 256) {
        model_warnings.push_back(block.name + ": " + warning);
      }
    }

    block.liquid = is_liquid(block.name);
    const Visual base_visual = make_visual(model, block.name);
    if (is_air(block.name)) {
      block.lifecycle = "air";
      block.render_class = "skip";
    } else if (block.liquid) {
      // 26.3 fluid models are visual stubs with only a particle texture; use
      // the real still-fluid sprite on every face for M1's full-cube water.
      const std::string fluid_sprite =
          block.name == "minecraft:water" || block.name == "minecraft:flowing_water"
              ? "block/water_still"
              : "block/lava_still";
      block.full_block = true;
      block.collision = false;
      block.lifecycle = "fluid";
      block.render_class = block.name == "minecraft:water" ? "transparent" : "opaque";
      for (int i = 0; i < kFaceCount; ++i) {
        block.sprites[kFaceNames[i]] = fluid_sprite;
        block.tints[kFaceNames[i]] = 0xFFFFFFFFu;
        const TintKind kind = block.name == "minecraft:water" ? TintKind::water
                                                              : TintKind::none;
        if (const char* kind_name = tint_kind_name(kind)) {
          block.tint_kinds[kFaceNames[i]] = kind_name;
        }
      }
    } else if (base_visual.render_class != "skip") {
      block.lifecycle = model.full_cube ? "cube" : "non_cube";
      block.full_block = base_visual.full_block;
      block.collision = base_visual.full_block && !block.liquid;
      block.render_class = base_visual.render_class;
      block.sprites = base_visual.sprites;
      block.tints = base_visual.tints;
      block.tint_kinds = base_visual.tint_kinds;
      block.geometry = base_visual.geometry;
    } else {
      block.lifecycle = "non_cube";
      block.render_class = "skip";
    }

    // State variants: at least grass_block[snowy=true] and snow layers, but
    // any blockstate whose models differ visually gets its own entry.
    if (!is_air(block.name) && !block.liquid && block.variant_count <= 16) {
      for (const auto& [state_key, variant_model] : state_info.variant_models) {
        if (state_key.empty()) continue;
        ResolvedModel variant;
        std::string variant_error;
        if (!resolver.resolve(variant_model, variant, variant_error)) continue;
        const Visual visual = make_visual(variant, block.name);
        if (visual.signature == base_visual.signature) continue;
        if (visual.render_class == "skip") continue;
        block.variants[state_key] = visual_to_json(visual, block.liquid);
      }
    }
    blocks.push_back(std::move(block));
  }
  std::sort(blocks.begin(), blocks.end(),
            [](const ExtractedBlock& a, const ExtractedBlock& b) {
              return a.name < b.name;
            });

  // Texture atlas: collect renderable sprites, pack and blit real client.jar
  // PNGs. Missing textures get a marked magenta checker and a report entry.
  std::set<std::string> sprite_set;
  auto add_face_sprites = [&](const Json& sprites) {
    if (!sprites.is_object()) return;
    for (auto it = sprites.begin(); it != sprites.end(); ++it) {
      if (it.value().is_string()) sprite_set.insert(it.value().get<std::string>());
    }
  };
  auto add_geometry_sprites = [&](const Json& geometry) {
    if (!geometry.is_array()) return;
    for (const Json& quad : geometry) {
      if (!quad.is_object()) continue;
      const auto sprite = quad.find("sprite");
      if (sprite != quad.end() && sprite->is_string()) {
        sprite_set.insert(sprite->get<std::string>());
      }
    }
  };
  for (const ExtractedBlock& block : blocks) {
    if (!block.renderable()) continue;
    for (int i = 0; i < kFaceCount; ++i) {
      const std::string sprite = sprite_value(block, i);
      if (!sprite.empty()) sprite_set.insert(sprite);
    }
    add_geometry_sprites(block.geometry);
    for (const auto& [state_key, variant] : block.variants) {
      (void)state_key;
      if (!variant.is_object()) continue;
      const auto sprites = variant.find("sprites");
      if (sprites != variant.end()) add_face_sprites(*sprites);
      const auto geometry = variant.find("geometry");
      if (geometry != variant.end()) add_geometry_sprites(*geometry);
    }
  }
  std::vector<std::string> sprites(sprite_set.begin(), sprite_set.end());
  const int per_row = kAtlasWidth / kTileSize;
  const int rows = std::max(1, static_cast<int>((sprites.size() + per_row - 1) / per_row));
  const int atlas_height = next_power_of_two(rows * kTileSize);
  if (atlas_height > kAtlasMaxHeight) {
    log_error("extract: atlas would exceed height limit");
    return 6;
  }
  std::vector<uint8_t> atlas(static_cast<size_t>(kAtlasWidth) * atlas_height * 4, 0);
  std::vector<std::string> missing_textures;
  for (size_t i = 0; i < sprites.size(); ++i) {
    const std::string& sprite = sprites[i];
    const int x = (static_cast<int>(i) % per_row) * kTileSize;
    const int y = (static_cast<int>(i) / per_row) * kTileSize;
    const std::string path = "assets/minecraft/textures/" + sprite + ".png";
    std::vector<uint8_t> bytes;
    png::Image image;
    std::string png_error;
    if (!archive.read(path, bytes, png_error) ||
        !png::load_memory(bytes.data(), bytes.size(), 4, image, png_error)) {
      draw_missing_checker(atlas, kAtlasWidth, x, y);
      missing_textures.push_back(sprite);
    } else {
      blit_texture(atlas, kAtlasWidth, x, y, image);
    }
  }
  if (!png::write_file(file::to_utf8(out_dir / "atlas.png"), kAtlasWidth, atlas_height,
                       4, atlas.data(), error)) {
    log_error(error);
    return 7;
  }
  file::Bytes atlas_bytes;
  if (!file::read_all(out_dir / "atlas.png", atlas_bytes, error)) {
    log_error(error);
    return 7;
  }
  const std::string atlas_sha256 =
      hash::hex(hash::sha256(atlas_bytes.data(), atlas_bytes.size()));

  // textures.json is written here (instead of write_generated) so the atlas
  // height and sprite order are final and deterministic.
  Json textures_json;
  textures_json["schema"] = "reim.textures";
  textures_json["format"] = 2;
  textures_json["profile"] = profile;
  textures_json["abi_version"] = REIM_ABI_VERSION;
  textures_json["atlas"] = Json{{"file", "atlas.png"},
                                {"width", kAtlasWidth},
                                {"height", atlas_height},
                                {"tile_size", kTileSize},
                                {"padding", 0},
                                {"sampling", "nearest"},
                                {"format", "rgba8"},
                                {"mipmaps", false},
                                {"sha256", atlas_sha256}};
  Json sprite_map = Json::object();
  for (size_t i = 0; i < sprites.size(); ++i) {
    const int index = static_cast<int>(i);
    const int x = (index % per_row) * kTileSize;
    const int y = (index / per_row) * kTileSize;
    Json entry;
    entry["index"] = index;
    entry["tile_x"] = index % per_row;
    entry["tile_y"] = index / per_row;
    entry["x"] = x;
    entry["y"] = y;
    entry["u0"] = static_cast<double>(x) / kAtlasWidth;
    entry["v0"] = static_cast<double>(y) / atlas_height;
    entry["u1"] = static_cast<double>(x + kTileSize) / kAtlasWidth;
    entry["v1"] = static_cast<double>(y + kTileSize) / atlas_height;
    sprite_map[sprites[i]] = entry;
  }
  textures_json["sprite_count"] = sprites.size();
  textures_json["sprites"] = sprite_map;
  if (!write_json_file(out_dir / "textures.json", textures_json, error)) {
    log_error(error);
    return 8;
  }

  const Json biome_tints = collect_biome_tints(archive, profile, model_warnings);
  if (!write_json_file(out_dir / "biome_tints.json", biome_tints, error)) {
    log_error(error);
    return 10;
  }

  if (!write_generated(out_dir, profile, profile_dir, inputs, blocks, sprites.size(),
                       kAtlasWidth, atlas_height, atlas_sha256, block_errors,
                       missing_textures, model_warnings)) {
    return 9;
  }

  log_info(std::format("extract: missing textures {}/{} (placeholder checker used)",
                       missing_textures.size(), sprites.size()));
  for (const std::string& warning : warnings) log_warn("extract: " + warning);
  return 0;
}

} // namespace reim::conv
