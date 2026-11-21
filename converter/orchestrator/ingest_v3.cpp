#include "ingest_v3.h"

#include "util/file.h"
#include "util/hash.h"
#include "util/png.h"
#include "util/zip.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reim::conv {
namespace {

using StringSet = std::set<std::string>;

std::string normalized(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  return value;
}

std::vector<std::filesystem::path> list_files(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) return files;
  for (std::filesystem::recursive_directory_iterator it(root, ec), end;
       it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file()) continue;
    files.push_back(std::filesystem::relative(it->path(), root, ec));
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string id_for(const std::filesystem::path& relative, const char* prefix) {
  std::string value = normalized(file::to_utf8(relative));
  const std::string suffix = ".json";
  if (value.size() >= suffix.size() &&
      value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
    value.resize(value.size() - suffix.size());
  }
  if (prefix && *prefix) {
    const std::string p(prefix);
    if (value.rfind(p, 0) == 0) value = value.substr(p.size());
  }
  return "minecraft:" + value;
}

bool read_json(const std::filesystem::path& path, Json& out, std::string& error) {
  return read_json_file(path, out, error);
}

bool write_generated_json(const std::filesystem::path& out_dir, const char* name,
                          const Json& value, std::string& error) {
  return write_json_file(out_dir / name, value, error);
}

const Json* member(const Json& object, const char* name) {
  if (!object.is_object()) return nullptr;
  const auto it = object.find(name);
  return it == object.end() ? nullptr : &(*it);
}

std::map<std::string, uint64_t> registry_protocol_ids(const Json& registries,
                                                      const char* registry_name) {
  std::map<std::string, uint64_t> out;
  const Json* registry = member(registries, registry_name);
  if (!registry) return out;
  const Json* entries = member(*registry, "entries");
  if (!entries || !entries->is_object()) return out;
  for (auto it = entries->begin(); it != entries->end(); ++it) {
    if (!it.value().is_object()) continue;
    out.emplace(it.key(), it.value().value("protocol_id", 0ull));
  }
  return out;
}

void collect_function_types(const Json& value, StringSet& out) {
  if (value.is_array()) {
    for (const Json& item : value) collect_function_types(item, out);
    return;
  }
  if (!value.is_object()) return;
  for (auto it = value.begin(); it != value.end(); ++it) {
    if (it.key() == "functions" && it.value().is_array()) {
      for (const Json& function : it.value()) {
        if (function.is_object()) {
          const auto type = function.find("type");
          if (type != function.end() && type->is_string()) {
            out.insert(type->get<std::string>());
          }
        }
      }
    }
    collect_function_types(it.value(), out);
  }
}

std::string sound_category(const std::string& event) {
  if (event.rfind("music.", 0) == 0) return "music";
  if (event.rfind("ui.", 0) == 0) return "ui";
  if (event.rfind("weather.", 0) == 0) return "weather";
  if (event.rfind("ambient.", 0) == 0) return "ambient";
  if (event.rfind("block.", 0) == 0) return "blocks";
  if (event.rfind("item.", 0) == 0) return "players";
  if (event.rfind("entity.player.", 0) == 0) return "players";
  if (event.rfind("entity.", 0) == 0) return "creatures";
  return "master";
}

uint32_t sound_category_id(const std::string& category) {
  if (category == "music") return 1;
  if (category == "blocks") return 2;
  if (category == "creatures") return 3;
  if (category == "ambient") return 4;
  if (category == "weather") return 5;
  if (category == "ui") return 6;
  if (category == "players") return 7;
  return 0;
}

Json sound_files_for_event(const std::string& event, const Json& definitions,
                           std::set<std::string>& visiting) {
  Json files = Json::array();
  if (visiting.count(event) != 0) return files;
  const Json* definition = member(definitions, event.c_str());
  if (!definition || !definition->is_object()) return files;
  visiting.insert(event);
  const Json* sounds = member(*definition, "sounds");
  if (sounds && sounds->is_array()) {
    for (const Json& item : *sounds) {
      std::string name;
      std::string type;
      double volume = 1.0;
      double pitch = 1.0;
      int weight = 1;
      bool stream = false;
      int attenuation = 16;
      if (item.is_string()) {
        name = item.get<std::string>();
      } else if (item.is_object()) {
        name = item.value("name", "");
        type = item.value("type", "");
        volume = item.value("volume", 1.0);
        pitch = item.value("pitch", 1.0);
        weight = item.value("weight", 1);
        stream = item.value("stream", false);
        attenuation = item.value("attenuation_distance", 16);
      }
      if (name.empty()) continue;
      if (type == "event") {
        const std::string child = normalize_resource(name);
        const Json nested = sound_files_for_event(child, definitions, visiting);
        for (const Json& file : nested) files.push_back(file);
        continue;
      }
      std::string file_name = normalize_resource(name);
      if (!(file_name.size() >= 4 && file_name.substr(file_name.size() - 4) == ".ogg")) {
        file_name += ".ogg";
      }
      Json entry;
      entry["path"] = "minecraft/sounds/" + file_name;
      entry["volume"] = volume;
      entry["pitch"] = pitch;
      entry["weight"] = weight;
      entry["stream"] = stream;
      entry["attenuation_distance"] = attenuation;
      files.push_back(std::move(entry));
    }
  }
  visiting.erase(event);
  return files;
}

bool copy_tree(const std::filesystem::path& source, const std::filesystem::path& dest,
               std::string& error) {
  if (!file::is_directory(source)) return true;
  for (const auto& relative : list_files(source)) {
    file::Bytes bytes;
    if (!file::read_all(source / relative, bytes, error)) return false;
    const auto target = dest / relative;
    if (!file::ensure_directory(target.parent_path(), error)) return false;
    if (!file::write_all(target, bytes.data(), bytes.size(), error)) return false;
  }
  return true;
}

int next_power_of_two(int value) {
  int out = 16;
  while (out < value) out <<= 1;
  return out;
}

/* Builds an item icon atlas from client.jar item textures (16x16). K6 requires
 * an item atlas; animated/non-16px textures are reported and skipped, never
 * silently faked. */
bool build_item_atlas(const std::filesystem::path& client_jar,
                      const std::filesystem::path& out_dir, Json& metadata,
                      std::string& error) {
  metadata = Json::object();
  metadata["schema"] = "reim.item_atlas";
  metadata["format"] = 3;
  metadata["source"] = "client.jar:assets/minecraft/textures/item";
  metadata["built"] = false;
  metadata["tile_size"] = 16;
  metadata["sprites"] = Json::object();
  metadata["skipped"] = Json::array();
  if (client_jar.empty() || !file::exists(client_jar)) {
    metadata["reason"] = "client JAR path unavailable";
    return true;
  }
  zip::Archive archive;
  if (!archive.open(file::to_utf8(client_jar), error)) {
    metadata["reason"] = error;
    return true;
  }
  std::vector<std::pair<std::string, png::Image>> images;
  Json skipped = Json::array();
  for (const std::string& entry : archive.list("assets/minecraft/textures/item/")) {
    if (!entry.ends_with(".png")) continue;
    std::vector<uint8_t> bytes;
    if (!archive.read(entry, bytes, error)) continue;
    png::Image image;
    std::string png_error;
    if (!png::load_memory(bytes.data(), bytes.size(), 4, image, png_error)) {
      skipped.push_back(entry + ": " + png_error);
      continue;
    }
    if (image.width != 16 || image.height != 16) {
      skipped.push_back(entry + ": size " + std::to_string(image.width) + "x" +
                        std::to_string(image.height) + " (16x16 only)");
      continue;
    }
    std::string name = entry.substr(std::string("assets/minecraft/textures/").size());
    name.resize(name.size() - 4);
    images.emplace_back(name, std::move(image));
  }
  if (images.empty()) {
    metadata["reason"] = "no 16x16 item textures found";
    metadata["skipped"] = std::move(skipped);
    return true;
  }
  std::sort(images.begin(), images.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  constexpr int kAtlasWidth = 2048;
  constexpr int kTile = 16;
  const int columns = kAtlasWidth / kTile;
  const int rows = static_cast<int>((images.size() + columns - 1) / columns);
  const int height = next_power_of_two(std::max(16, rows * kTile));
  std::vector<uint8_t> pixels(static_cast<size_t>(kAtlasWidth) * height * 4, 0);
  Json sprites = Json::object();
  int index = 0;
  for (const auto& [name, image] : images) {
    const int cell_x = (index % columns) * kTile;
    const int cell_y = (index / columns) * kTile;
    for (int y = 0; y < kTile; ++y) {
      for (int x = 0; x < kTile; ++x) {
        const uint8_t* source = image.at(x, y);
        uint8_t* target = pixels.data() +
            (static_cast<size_t>(cell_y + y) * kAtlasWidth + cell_x + x) * 4;
        target[0] = source[0];
        target[1] = source[1];
        target[2] = source[2];
        target[3] = source[3];
      }
    }
    Json sprite;
    sprite["index"] = index;
    sprite["u0"] = static_cast<double>(cell_x) / kAtlasWidth;
    sprite["v0"] = static_cast<double>(cell_y) / height;
    sprite["u1"] = static_cast<double>(cell_x + kTile) / kAtlasWidth;
    sprite["v1"] = static_cast<double>(cell_y + kTile) / height;
    sprites[name] = std::move(sprite);
    ++index;
  }
  const auto atlas_path = out_dir / "item_atlas.png";
  if (!png::write_file(file::to_utf8(atlas_path), kAtlasWidth, height, 4,
                       pixels.data(), error)) {
    return false;
  }
  metadata["built"] = true;
  metadata["width"] = kAtlasWidth;
  metadata["height"] = height;
  metadata["sprite_count"] = images.size();
  metadata["sprites"] = std::move(sprites);
  metadata["skipped"] = std::move(skipped);
  metadata["sha256"] = [&]() {
    file::Bytes bytes;
    std::string ignored;
    if (!file::read_all(atlas_path, bytes, ignored)) return std::string();
    return hash::hex(hash::sha256(bytes.data(), bytes.size()));
  }();
  return true;
}

} // namespace

bool augment_generated_v3(const std::filesystem::path& out_dir,
                          const std::string& profile,
                          const std::filesystem::path& profile_dir,
                          V3Summary& summary, std::string& error) {
  const auto reports_dir = profile_dir / "reports" / "reports";
  const auto datapack_dir = profile_dir / "datapack";
  const auto assets_dir = profile_dir / "assets";
  const auto minecraft_data = datapack_dir / "data" / "minecraft";
  if (!file::is_directory(reports_dir)) {
    error = "K1 data missing: run `reim-convert data` first (" +
            file::to_utf8(reports_dir) + ")";
    return false;
  }
  if (!file::is_directory(minecraft_data)) {
    error = "K1 datapack missing: run `reim-convert data` first (" +
            file::to_utf8(minecraft_data) + ")";
    return false;
  }

  Json registries;
  if (!read_json(reports_dir / "registries.json", registries, error)) return false;
  Json report_blocks;
  if (!read_json(reports_dir / "blocks.json", report_blocks, error)) return false;

  const auto block_ids = registry_protocol_ids(registries, "minecraft:block");

  // ---- tags first: items reference them ----------------------------------
  Json tags;
  tags["schema"] = "reim.tags";
  tags["format"] = 3;
  tags["profile"] = profile;
  tags["source"] = "server.jar built-in datapack: data/minecraft/tags";
  Json tag_types = Json::object();
  uint64_t tag_count = 0;
  for (const auto& relative :
       list_files(minecraft_data / "tags")) {
    if (!relative.string().ends_with(".json")) continue;
    Json root;
    if (!read_json(minecraft_data / "tags" / relative, root, error)) return false;
    const std::string path = normalized(file::to_utf8(relative));
    const size_t slash = path.find('/');
    const std::string type = slash == std::string::npos ? "unknown" : path.substr(0, slash);
    std::string id = path;
    id.resize(id.size() - 5);
    Json values = Json::array();
    if (const Json* list = member(root, "values"); list && list->is_array()) {
      std::vector<std::string> sorted;
      for (const Json& value : *list) {
        if (value.is_string()) sorted.push_back(value.get<std::string>());
      }
      std::sort(sorted.begin(), sorted.end());
      for (const std::string& value : sorted) values.push_back(value);
    }
    tags["types"][type]["minecraft:" + id] = std::move(values);
    ++tag_count;
  }
  summary.tags = tag_count;
  if (!write_generated_json(out_dir, "tags.json", tags, error)) return false;

  // ---- K2: blocks state ids ----------------------------------------------
  Json blocks;
  if (!read_json(out_dir / "blocks.json", blocks, error)) return false;
  uint64_t state_total = 0;
  for (auto it = blocks["blocks"].begin(); it != blocks["blocks"].end(); ++it) {
    if (!it.value().is_object()) continue;
    const auto report_it = report_blocks.find(it.key());
    if (report_it == report_blocks.end() || !report_it->is_object()) continue;
    const auto protocol = block_ids.find(it.key());
    if (protocol != block_ids.end()) it.value()["protocol_id"] = protocol->second;
    const Json* states = member(*report_it, "states");
    if (states && states->is_array()) {
      Json state_ids = Json::array();
      for (const Json& state : *states) {
        if (!state.is_object()) continue;
        Json entry;
        entry["id"] = state.value("id", 0);
        const Json* properties = member(state, "properties");
        entry["properties"] = properties ? *properties : Json::object();
        state_ids.push_back(std::move(entry));
        ++state_total;
      }
      it.value()["state_ids"] = std::move(state_ids);
      it.value()["state_count"] = states->size();
    }
    it.value()["data_source"] =
        "client.jar assets (visuals) + reports/blocks.json (state ids)";
  }
  blocks["format"] = 3;
  blocks["schema_version"] = 3;
  blocks["reports_sha256"] = [&]() {
    file::Bytes bytes;
    std::string ignored;
    if (!file::read_all(reports_dir / "blocks.json", bytes, ignored)) return std::string();
    return hash::hex(hash::sha256(bytes.data(), bytes.size()));
  }();
  summary.block_states = state_total;
  if (!write_json_file(out_dir / "blocks.json", blocks, error)) return false;

  // ---- K2: items ----------------------------------------------------------
  const auto item_ids = registry_protocol_ids(registries, "minecraft:item");
  const StringSet native_items = {
      "minecraft:wooden_pickaxe", "minecraft:stone_pickaxe", "minecraft:iron_pickaxe",
      "minecraft:diamond_pickaxe", "minecraft:wooden_axe", "minecraft:stone_axe",
      "minecraft:iron_axe", "minecraft:diamond_axe", "minecraft:wooden_shovel",
      "minecraft:stone_shovel", "minecraft:iron_shovel", "minecraft:diamond_shovel",
      "minecraft:wooden_sword", "minecraft:stone_sword", "minecraft:iron_sword",
      "minecraft:diamond_sword", "minecraft:wooden_hoe", "minecraft:stone_hoe",
      "minecraft:iron_hoe", "minecraft:diamond_hoe", "minecraft:bread",
      "minecraft:apple", "minecraft:cooked_beef", "minecraft:cooked_porkchop",
      "minecraft:cooked_chicken", "minecraft:cooked_mutton", "minecraft:coal",
      "minecraft:charcoal", "minecraft:iron_ingot", "minecraft:gold_ingot",
      "minecraft:diamond", "minecraft:stick", "minecraft:torch", "minecraft:arrow",
      "minecraft:bow",
  };
  Json items;
  items["schema"] = "reim.items";
  items["format"] = 3;
  items["profile"] = profile;
  items["source"] = "reports/registries.json + server datapack tags; "
                    "attributes marked data_only unless listed native";
  Json item_entries = Json::object();
  for (const auto& [name, protocol] : item_ids) {
    Json entry;
    entry["protocol_id"] = protocol;
    entry["block"] = blocks["blocks"].contains(name);
    entry["max_stack"] = 64;
    entry["max_damage"] = 0;
    const bool is_native = native_items.count(name) != 0;
    entry["runtime_native"] = is_native;
    entry["runtime_source"] = is_native
                                  ? std::string("native table (Java code source; M3-D3)")
                                  : std::string("data_only (no behavior table in this build)");
    entry["tags"] = Json::array();
    if (const Json* item_tags = member(tags["types"], "item");
        item_tags && item_tags->is_object()) {
      for (auto t = item_tags->begin(); t != item_tags->end(); ++t) {
        if (!t.value().is_array()) continue;
        for (const Json& value : t.value()) {
          if (value.is_string() && value.get<std::string>() == name) {
            entry["tags"].push_back(t.key());
            break;
          }
        }
      }
    }
    item_entries[name] = std::move(entry);
  }
  items["item_count"] = item_ids.size();
  items["native_count"] = native_items.size();
  items["items"] = std::move(item_entries);
  summary.items = item_ids.size();
  if (!write_generated_json(out_dir, "items.json", items, error)) return false;

  // ---- K2: entities -------------------------------------------------------
  const auto entity_ids = registry_protocol_ids(registries, "minecraft:entity_type");
  const std::map<std::string, int> native_entities = {
      {"minecraft:zombie", 1},   {"minecraft:skeleton", 2},
      {"minecraft:creeper", 3},  {"minecraft:spider", 4},
      {"minecraft:cow", 5},      {"minecraft:pig", 6},
      {"minecraft:sheep", 7},    {"minecraft:chicken", 8},
  };
  Json entities;
  entities["schema"] = "reim.entities";
  entities["format"] = 3;
  entities["profile"] = profile;
  entities["source"] = "reports/registries.json; native models are Java code "
                       "(game/entity.cpp), not data extraction";
  Json entity_entries = Json::object();
  for (const auto& [name, protocol] : entity_ids) {
    Json entry;
    entry["protocol_id"] = protocol;
    const auto native = native_entities.find(name);
    entry["native"] = native != native_entities.end();
    if (native != native_entities.end()) {
      entry["model_id"] = native->second;
      entry["model_source"] = "runtime/mc-native/game/entity.cpp";
      ++summary.native_entities;
    } else {
      entry["supported"] = false;
      entry["reason"] = "not in the M3 8-mob set";
    }
    entity_entries[name] = std::move(entry);
  }
  entities["entity_count"] = entity_ids.size();
  entities["native_count"] = summary.native_entities;
  entities["entities"] = std::move(entity_entries);
  summary.entities = entity_ids.size();
  if (!write_generated_json(out_dir, "entities.json", entities, error)) return false;

  // ---- K3: model summary --------------------------------------------------
  Json models;
  models["schema"] = "reim.models";
  models["format"] = 3;
  models["profile"] = profile;
  models["source"] = "client.jar assets/minecraft/blockstates + models; resolved "
                     "geometry is in blocks.json";
  models["limitations"] = Json::array(
      {"multipart/weight metadata is resolved into variants",
       "elements/rotation/uv/cullface are baked into blocks.json geometry"});
  Json model_entries = Json::object();
  StringSet model_names;
  uint64_t variant_total = 0;
  uint64_t geometry_total = 0;
  for (auto it = blocks["blocks"].begin(); it != blocks["blocks"].end(); ++it) {
    if (!it.value().is_object()) continue;
    Json entry;
    const Json* model = member(it.value(), "model");
    if (model && model->is_string()) {
      entry["model"] = *model;
      model_names.insert(model->get<std::string>());
    }
    entry["variant_count"] = it.value().value("variant_count", 0);
    entry["state_count"] = it.value().value("state_count", 0);
    variant_total += it.value().value("variant_count", 0ull);
    const Json* geometry = member(it.value(), "geometry");
    entry["geometry_quads"] = geometry && geometry->is_array() ? geometry->size() : 0;
    geometry_total += entry["geometry_quads"].get<uint64_t>();
    entry["render_class"] = it.value().value("render_class", "skip");
    model_entries[it.key()] = std::move(entry);
  }
  models["model_count"] = model_names.size();
  models["block_count"] = model_entries.size();
  models["variant_total"] = variant_total;
  models["geometry_quad_total"] = geometry_total;
  models["blocks"] = std::move(model_entries);
  summary.models = model_names.size();
  if (!write_generated_json(out_dir, "models.json", models, error)) return false;

  // ---- K4: recipes --------------------------------------------------------
  const StringSet supported_recipe_types = {
      "minecraft:crafting_shaped", "minecraft:crafting_shapeless",
      "minecraft:smelting", "minecraft:blasting", "minecraft:smoking",
      "minecraft:campfire_cooking", "minecraft:stonecutting",
      "minecraft:smithing_transform",
  };
  Json recipes;
  recipes["schema"] = "reim.recipes";
  recipes["format"] = 3;
  recipes["profile"] = profile;
  recipes["source"] = "server.jar built-in datapack: data/minecraft/recipe";
  Json recipe_entries = Json::object();
  Json recipe_types = Json::object();
  StringSet unsupported_recipe_types;
  for (const auto& relative : list_files(minecraft_data / "recipe")) {
    if (!relative.string().ends_with(".json")) continue;
    Json root;
    if (!read_json(minecraft_data / "recipe" / relative, root, error)) return false;
    const std::string id = id_for(relative, "");
    const std::string type = root.value("type", "");
    const bool supported = supported_recipe_types.count(type) != 0;
    Json entry;
    entry["id"] = id;
    entry["type"] = type;
    entry["supported"] = supported;
    entry["data"] = std::move(root);
    recipe_entries[id] = std::move(entry);
    recipe_types[type] = recipe_types.value(type, 0) + 1;
    if (supported) ++summary.recipes_supported;
    else unsupported_recipe_types.insert(type);
  }
  summary.recipes = recipe_entries.size();
  recipes["recipe_count"] = recipe_entries.size();
  recipes["supported_count"] = summary.recipes_supported;
  recipes["by_type"] = std::move(recipe_types);
  Json unsupported_recipe_json = Json::array();
  for (const std::string& type : unsupported_recipe_types) {
    unsupported_recipe_json.push_back(type);
  }
  recipes["unsupported_types"] = std::move(unsupported_recipe_json);
  recipes["recipes"] = std::move(recipe_entries);
  if (!write_generated_json(out_dir, "recipes.json", recipes, error)) return false;

  // ---- K4: loot tables ----------------------------------------------------
  const StringSet supported_loot_functions = {
      "minecraft:set_count", "minecraft:limit_count",
      "minecraft:apply_bonus", "minecraft:explosion_decay",
  };
  Json loot;
  loot["schema"] = "reim.loot_tables";
  loot["format"] = 3;
  loot["profile"] = profile;
  loot["source"] = "server.jar built-in datapack: data/minecraft/loot_table";
  loot["runtime_note"] =
      "Generated data is ingested for diff/patch; Runtime block/entity loot "
      "currently uses the native M3 subset (M3-E6).";
  Json loot_entries = Json::object();
  StringSet all_loot_functions;
  StringSet unsupported_loot_functions;
  uint64_t loot_unsupported_tables = 0;
  for (const auto& relative : list_files(minecraft_data / "loot_table")) {
    if (!relative.string().ends_with(".json")) continue;
    Json root;
    if (!read_json(minecraft_data / "loot_table" / relative, root, error)) return false;
    StringSet functions;
    collect_function_types(root, functions);
    bool supported = true;
    for (const std::string& function : functions) {
      all_loot_functions.insert(function);
      if (supported_loot_functions.count(function) == 0) {
        unsupported_loot_functions.insert(function);
        supported = false;
      }
    }
    if (!supported) ++loot_unsupported_tables;
    const std::string id = id_for(relative, "");
    Json entry;
    entry["id"] = id;
    entry["supported_functions"] = supported;
    Json function_list = Json::array();
    for (const std::string& function : functions) function_list.push_back(function);
    entry["functions"] = std::move(function_list);
    entry["data"] = std::move(root);
    loot_entries[id] = std::move(entry);
  }
  summary.loot_tables = loot_entries.size();
  summary.loot_unsupported = loot_unsupported_tables;
  loot["table_count"] = loot_entries.size();
  loot["unsupported_table_count"] = loot_unsupported_tables;
  Json unsupported_functions_json = Json::array();
  for (const std::string& function : unsupported_loot_functions) {
    unsupported_functions_json.push_back(function);
  }
  loot["unsupported_functions"] = std::move(unsupported_functions_json);
  loot["tables"] = std::move(loot_entries);
  if (!write_generated_json(out_dir, "loot_tables.json", loot, error)) return false;

  // ---- K5: worldgen -------------------------------------------------------
  Json worldgen;
  worldgen["schema"] = "reim.worldgen";
  worldgen["format"] = 3;
  worldgen["profile"] = profile;
  worldgen["source"] = "server.jar built-in datapack: data/minecraft/worldgen, "
                       "dimension, dimension_type";
  worldgen["runtime_loader"] =
      "validated by the Runtime generated-manifest loader; generation remains "
      "the native profile until a data profile is wired (G1/K5 follow-up)";
  Json worldgen_types = Json::object();
  uint64_t worldgen_count = 0;
  for (const auto& relative : list_files(minecraft_data / "worldgen")) {
    if (!relative.string().ends_with(".json")) continue;
    Json root;
    if (!read_json(minecraft_data / "worldgen" / relative, root, error)) return false;
    const std::string path = normalized(file::to_utf8(relative));
    const size_t slash = path.find('/');
    const std::string type = slash == std::string::npos ? "unknown" : path.substr(0, slash);
    std::string id = path;
    id.resize(id.size() - 5);
    worldgen_types[type]["minecraft:" + id] = std::move(root);
    ++worldgen_count;
  }
  for (const char* directory : {"dimension", "dimension_type"}) {
    for (const auto& relative : list_files(minecraft_data / directory)) {
      if (!relative.string().ends_with(".json")) continue;
      Json root;
      if (!read_json(minecraft_data / directory / relative, root, error)) return false;
      const std::string id = id_for(relative, "");
      worldgen_types[directory][id] = std::move(root);
      ++worldgen_count;
    }
  }
  summary.worldgen = worldgen_count;
  worldgen["definition_count"] = worldgen_count;
  worldgen["types"] = std::move(worldgen_types);
  {
    Json parameters = Json::object();
    parameters["count"] = list_files(reports_dir / "biome_parameters").size();
    worldgen["biome_parameters"] = std::move(parameters);
  }
  if (!write_generated_json(out_dir, "worldgen.json", worldgen, error)) return false;

  // ---- K6: sounds ---------------------------------------------------------
  Json sound_definitions;
  const auto sounds_path = assets_dir / "minecraft" / "sounds.json";
  if (!read_json(sounds_path, sound_definitions, error)) return false;
  const auto sound_event_ids =
      registry_protocol_ids(registries, "minecraft:sound_event");
  Json sounds;
  sounds["schema"] = "reim.sounds";
  sounds["format"] = 3;
  sounds["profile"] = profile;
  sounds["source"] = "client assets sounds.json + reports/registries.json "
                     "(sound_event); OGG files are referenced from assets";
  struct M3Sound {
    uint32_t id;
    const char* event;
  };
  const M3Sound m3_sounds[] = {
      {1, "ui.button.click"},          {10, "entity.generic.hurt"},
      {11, "entity.generic.death"},    {12, "entity.generic.explode"},
      {13, "entity.player.hurt"},      {14, "entity.player.attack.strong"},
      {15, "entity.creeper.primed"},   {16, "entity.player.death"},
      {20, "block.stone.break"},       {21, "block.stone.place"},
      {22, "block.stone.step"},        {23, "entity.item.pickup"},
      {24, "block.stone.hit"},         {25, "weather.rain"},
      {26, "entity.lightning_bolt.thunder"}, {27, "ambient.cave"},
      {28, "entity.experience_orb.pickup"},
  };
  Json m3 = Json::array();
  for (const M3Sound& sound : m3_sounds) {
    const std::string category = sound_category(sound.event);
    Json entry;
    entry["id"] = sound.id;
    entry["event"] = sound.event;
    entry["category"] = sound_category_id(category);
    entry["category_name"] = category;
    std::set<std::string> visiting;
    Json files = sound_files_for_event(sound.event, sound_definitions, visiting);
    for (Json& file : files) {
      const std::string path = file.value("path", "");
      file["local"] = file::exists(assets_dir / file::from_utf8(path));
    }
    entry["files"] = std::move(files);
    m3.push_back(std::move(entry));
  }
  sounds["m3"] = std::move(m3);
  Json registry = Json::object();
  for (const auto& [name, protocol] : sound_event_ids) {
    registry[name] = Json{{"protocol_id", protocol},
                          {"category", sound_category_id(sound_category(name))},
                          {"category_name", sound_category(name)}};
  }
  sounds["registry_count"] = registry.size();
  sounds["registry"] = std::move(registry);
  sounds["definitions"] = std::move(sound_definitions);
  summary.sounds = sound_event_ids.size();
  if (!write_generated_json(out_dir, "sounds.json", sounds, error)) return false;

  /* Make the M3-selected binaries self-contained: sounds/lang/font copied by
   * `reim-convert data` live under <profile>/assets. The Runtime resolves
   * SOUND_OGG against generated/assets/<asset path>. */
  uint64_t local_binary_count = 0;
  {
    Json asset_index;
    if (read_json(profile_dir / "assets_index.json", asset_index, error)) {
      const Json* objects = member(asset_index, "objects");
      if (objects && objects->is_array()) {
        for (const Json& object : *objects) {
          if (!object.value("local", false)) continue;
          const std::string path = object.value("path", "");
          if (path.empty()) continue;
          std::string safe_error;
          if (!safe_archive_path(path, safe_error)) {
            error = safe_error;
            return false;
          }
          file::Bytes bytes;
          if (!file::read_all(profile_dir / "assets" / file::from_utf8(path), bytes,
                              error)) {
            return false;
          }
          const auto dest = out_dir / "assets" / file::from_utf8(path);
          if (!file::ensure_directory(dest.parent_path(), error)) return false;
          if (!file::write_all(dest, bytes.data(), bytes.size(), error)) return false;
          ++local_binary_count;
        }
      }
    } else {
      error.clear();
    }
  }

  // ---- K6: lang / font / item atlas / atlases ----------------------------
  const auto lang_source = assets_dir / "minecraft" / "lang" / "en_us.json";
  Json lang_values;
  if (!read_json(lang_source, lang_values, error)) return false;
  if (!file::ensure_directory(out_dir / "lang", error)) return false;
  {
    file::Bytes bytes;
    if (!file::read_all(lang_source, bytes, error)) return false;
    if (!file::write_all(out_dir / "lang" / "en_us.json", bytes.data(), bytes.size(),
                         error)) {
      return false;
    }
  }
  Json lang;
  lang["schema"] = "reim.lang";
  lang["format"] = 3;
  lang["language"] = "en_us";
  lang["fallback"] = "en_us";
  lang["entry_count"] = lang_values.is_object() ? lang_values.size() : 0;
  lang["file"] = "lang/en_us.json";
  lang["cjk"] = false;
  lang["cjk_note"] = "CJK fonts are not included in M3; English fallback is explicit";
  summary.lang_entries = lang["entry_count"].get<uint64_t>();
  if (!write_generated_json(out_dir, "lang.json", lang, error)) return false;

  if (!copy_tree(assets_dir / "minecraft" / "font", out_dir / "font", error)) {
    return false;
  }
  if (!copy_tree(assets_dir / "minecraft" / "textures" / "font",
                 out_dir / "font" / "textures", error)) {
    return false;
  }
  Json font;
  font["schema"] = "reim.font";
  font["format"] = 3;
  font["source"] = "client assets font/*.json + textures/font";
  font["runtime_font"] = "synthesized 5x7 atlas (UI)";
  font["cjk"] = false;
  Json providers = Json::array();
  Json default_font;
  if (read_json(assets_dir / "minecraft" / "font" / "default.json", default_font,
                error)) {
    if (const Json* list = member(default_font, "providers"); list && list->is_array()) {
      for (const Json& provider : *list) providers.push_back(provider);
    }
  }
  font["provider_count"] = providers.size();
  font["providers"] = std::move(providers);
  font["files"] = static_cast<uint64_t>(list_files(out_dir / "font").size());
  summary.fonts = font["files"].get<uint64_t>();
  if (!write_generated_json(out_dir, "font.json", font, error)) return false;

  Json item_atlas;
  std::filesystem::path client_jar;
  {
    Json input_manifest;
    if (read_json(profile_dir / "input_manifest.json", input_manifest, error)) {
      client_jar = file::from_utf8(input_manifest.value("client_jar", ""));
    }
    error.clear();
  }
  if (!build_item_atlas(client_jar, out_dir, item_atlas, error)) return false;
  item_atlas["profile"] = profile;
  if (!write_generated_json(out_dir, "item_atlas.json", item_atlas, error)) return false;

  Json ui_atlas;
  ui_atlas["schema"] = "reim.ui_atlas";
  ui_atlas["format"] = 3;
  ui_atlas["built"] = false;
  ui_atlas["note"] =
      "M3 UI is a Runtime display list of rects/textured quads/glyphs; no "
      "vanilla UI texture atlas is consumed.";
  if (!write_generated_json(out_dir, "ui_atlas.json", ui_atlas, error)) return false;

  // ---- asset index copy ---------------------------------------------------
  Json asset_index;
  if (read_json(profile_dir / "assets_index.json", asset_index, error)) {
    asset_index["schema"] = "reim.asset_index";
    asset_index["generated_copy"] = true;
    if (!write_generated_json(out_dir, "assets_index.json", asset_index, error)) {
      return false;
    }
  } else {
    error.clear();
  }

  // ---- biomes -------------------------------------------------------------
  Json biome_registry = Json::object();
  const auto biome_ids = registry_protocol_ids(registries, "minecraft:biome");
  for (const auto& [name, protocol] : biome_ids) {
    biome_registry[name] = protocol;
  }
  Json tint_meta;
  Json biomes;
  uint64_t tint_count = 0;
  if (read_json(out_dir / "biome_tints.json", tint_meta, error)) {
    if (const Json* biomes_node = member(tint_meta, "biomes"); biomes_node &&
        biomes_node->is_object()) {
      tint_count = biomes_node->size();
    }
    error.clear();
  }
  biomes["schema"] = "reim.biomes";
  biomes["format"] = 3;
  biomes["profile"] = profile;
  biomes["source"] = "reports/registries.json + client colormaps";
  biomes["registry_count"] = biome_registry.size();
  biomes["tinted_biome_count"] = tint_count;
  biomes["entries"] = std::move(biome_registry);
  if (!write_generated_json(out_dir, "biomes.json", biomes, error)) return false;

  // ---- K9: coverage report ------------------------------------------------
  Json unsupported_recipes = Json::array();
  for (const std::string& type : unsupported_recipe_types) unsupported_recipes.push_back(type);
  Json unsupported_loot = Json::array();
  for (const std::string& function : unsupported_loot_functions) {
    unsupported_loot.push_back(function);
  }
  Json coverage;
  coverage["schema"] = "reim.coverage_report";
  coverage["format"] = 3;
  coverage["profile"] = profile;
  coverage["sources"] = Json{
      {"block_states", "reports/blocks.json"},
      {"block_visuals", "client.jar assets"},
      {"items", "reports/registries.json (attributes data_only)"},
      {"entities", "reports/registries.json (models are Java code)"},
      {"recipes", "server datapack"},
      {"loot_tables", "server datapack"},
      {"tags", "server datapack"},
      {"worldgen", "server datapack"},
      {"sounds", "client sounds.json + report registry"},
      {"lang", "client assets/client.jar"},
      {"atlas", "client.jar block/item textures"},
  };
  coverage["counts"] = Json{
      {"blocks", blocks["blocks"].size()},
      {"block_states", state_total},
      {"items", summary.items},
      {"native_items", native_items.size()},
      {"entities", summary.entities},
      {"native_entities", summary.native_entities},
      {"models", summary.models},
      {"recipes", summary.recipes},
      {"recipes_supported", summary.recipes_supported},
      {"loot_tables", summary.loot_tables},
      {"loot_unsupported", summary.loot_unsupported},
      {"tags", summary.tags},
      {"worldgen", summary.worldgen},
      {"sound_events", summary.sounds},
      {"lang_entries", summary.lang_entries},
      {"fonts", summary.fonts},
      {"item_atlas_sprites", item_atlas.value("sprite_count", 0ull)},
      {"local_binary_assets", local_binary_count},
  };
  coverage["unsupported"] = Json{
      {"recipe_types", std::move(unsupported_recipes)},
      {"loot_functions", std::move(unsupported_loot)},
      {"entities", "not in the M3 8-mob set are data_only"},
      {"items", "items without a native behavior row are data_only"},
      {"item_atlas", item_atlas["skipped"]},
  };
  coverage["java_code_sources"] = Json::array(
      {"entity models/animations/AI/spawn rules (game/entity.cpp)",
       "block behavior/collision/light/tool tables (native M3 subset)",
       "item attributes/fuel/food (native M3 subset)",
       "worldgen profile (world/worldgen.cpp) is native until a data profile is wired"});
  coverage["hidden_absolute_paths"] = false;
  coverage["notes"] = Json::array(
      {"Generated data is not committed or shipped; Mojang inputs stay on the "
       "developer machine.",
       "data_only rows are ingested and diffable but do not add gameplay."});
  summary.coverage = coverage;
  if (!write_generated_json(out_dir, "coverage_report.json", coverage, error)) {
    return false;
  }

  log_info("extract: schema v3 data ingested (items " +
           std::to_string(summary.items) + ", recipes " +
           std::to_string(summary.recipes) + ", loot " +
           std::to_string(summary.loot_tables) + ", worldgen " +
           std::to_string(summary.worldgen) + ", sounds " +
           std::to_string(summary.sounds) + ")");
  return true;
}

} // namespace reim::conv
