#include "model_resolver.h"

#include <cmath>
#include <format>
#include <set>

namespace reim::conv {

const char* const kFaceNames[kFaceCount] = {"down", "up", "north",
                                            "south", "west", "east"};

namespace {

bool nearly(float a, float b) { return std::fabs(a - b) < 1e-4f; }

bool read_vec3(const Json& value, float out[3]) {
  if (!value.is_array() || value.size() != 3) return false;
  for (int i = 0; i < 3; ++i) {
    if (!value[i].is_number()) return false;
    out[i] = value[i].get<float>();
  }
  return true;
}

std::string get_string(const Json& object, const char* name) {
  if (!object.is_object()) return {};
  const auto it = object.find(name);
  if (it == object.end() || !it->is_string()) return {};
  return it->get<std::string>();
}

} // namespace

ModelResolver::ModelResolver(const zip::Archive& zip, std::vector<std::string>& warnings)
    : zip_(zip), warnings_(warnings) {}

std::string ModelResolver::normalize_model(const std::string& model_ref) {
  std::string name = model_ref;
  const size_t colon = name.find(':');
  if (colon != std::string::npos) name = name.substr(colon + 1);
  while (!name.empty() && name.front() == '/') name.erase(name.begin());
  if (name.size() < 5 || name.substr(name.size() - 5) != ".json") name += ".json";
  return name;
}

const ModelResolver::ModelDoc* ModelResolver::load_doc(const std::string& normalized) {
  auto existing = cache_.find(normalized);
  if (existing != cache_.end()) return existing->second.get();

  auto doc = std::make_unique<ModelDoc>();
  const std::string path = "assets/minecraft/models/" + normalized;
  std::vector<uint8_t> bytes;
  std::string error;
  if (!zip_.read(path, bytes, error)) {
    doc->error = error;
    auto* raw = doc.get();
    cache_.emplace(normalized, std::move(doc));
    return raw;
  }
  doc->json = parse_json_noexcept(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  if (doc->json.is_discarded() || !doc->json.is_object()) {
    doc->error = "invalid model JSON: " + path;
  } else {
    doc->valid = true;
  }
  auto* raw = doc.get();
  cache_.emplace(normalized, std::move(doc));
  return raw;
}

std::string ModelResolver::resolve_texture(
    const std::string& value,
    const std::unordered_map<std::string, std::string>& textures, int depth) {
  if (depth > 16) return {};
  std::string current = value;
  if (!current.empty() && current.front() == '#') {
    const std::string key = current.substr(1);
    const auto it = textures.find(key);
    if (it == textures.end()) return {};
    return resolve_texture(it->second, textures, depth + 1);
  }
  const size_t colon = current.find(':');
  if (colon != std::string::npos) current = current.substr(colon + 1);
  if (current.empty()) return {};
  if (current.find('/') == std::string::npos) current = "block/" + current;
  return current;
}

bool ModelResolver::is_full_cube_element(const Json& element) {
  if (!element.is_object()) return false;
  float from[3];
  float to[3];
  if (!element.contains("from") || !element.contains("to")) return false;
  if (!read_vec3(element["from"], from) || !read_vec3(element["to"], to)) return false;
  if (!nearly(from[0], 0.0f) || !nearly(from[1], 0.0f) || !nearly(from[2], 0.0f)) {
    return false;
  }
  if (!nearly(to[0], 16.0f) || !nearly(to[1], 16.0f) || !nearly(to[2], 16.0f)) {
    return false;
  }
  if (element.contains("rotation") && element["rotation"].is_object()) {
    const Json& rotation = element["rotation"];
    if (rotation.contains("angle") && rotation["angle"].is_number()) {
      if (!nearly(rotation["angle"].get<float>(), 0.0f)) return false;
    }
  }
  return true;
}

uint32_t ModelResolver::default_tint(const std::string& sprite) {
  // M1 keeps the Runtime's tint semantics simple: grass tops and foliage use
  // the vanilla default biome colors. Per-biome tint arrives with biome data.
  if (sprite == "block/grass_block_top") return 0xFF79C05Au;
  if (sprite == "block/vine") return 0xFF6AA84Fu;
  if (sprite.ends_with("_leaves")) {
    if (sprite == "block/birch_leaves") return 0xFF80A755u;
    if (sprite == "block/spruce_leaves") return 0xFF619961u;
    if (sprite == "block/jungle_leaves") return 0xFF59C93Cu;
    if (sprite == "block/acacia_leaves") return 0xFF77AC2Fu;
    if (sprite == "block/dark_oak_leaves") return 0xFF59AE30u;
    if (sprite == "block/cherry_leaves") return 0xFFF6B6D9u;
    if (sprite == "block/azalea_leaves") return 0xFF6FA552u;
    if (sprite == "block/flowering_azalea_leaves") return 0xFFC08BC2u;
    return 0xFF6AA84Fu;
  }
  return 0xFFFFFFFFu;
}

bool ModelResolver::resolve(const std::string& model_ref, ResolvedModel& out,
                            std::string& error) {
  out = ResolvedModel{};
  out.model_ref = model_ref;
  if (model_ref.empty()) {
    error = "empty model reference";
    return false;
  }

  std::vector<const Json*> chain;
  std::set<std::string> visited;
  std::string current = normalize_model(model_ref);
  while (!current.empty() && visited.insert(current).second && chain.size() < 32) {
    const ModelDoc* doc = load_doc(current);
    if (!doc->valid) {
      if (chain.empty()) {
        error = doc->error;
        return false;
      }
      out.warnings.push_back("missing parent model " + current);
      break;
    }
    chain.push_back(&doc->json);
    const std::string parent = get_string(doc->json, "parent");
    current = parent.empty() ? std::string() : normalize_model(parent);
  }

  std::unordered_map<std::string, std::string> textures;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const Json& model = **it;
    if (!model.contains("textures") || !model["textures"].is_object()) continue;
    for (auto tex = model["textures"].begin(); tex != model["textures"].end(); ++tex) {
      if (tex.value().is_string()) {
        textures[tex.key()] = tex.value().get<std::string>();
      } else if (tex.value().is_object()) {
        // 26.3 resource metadata: {"sprite": "...", "force_translucent": true}
        const Json& value = tex.value();
        if (value.contains("sprite") && value["sprite"].is_string()) {
          textures[tex.key()] = value["sprite"].get<std::string>();
        }
      }
    }
  }

  const Json* elements = nullptr;
  for (const Json* model : chain) {
    if (model->contains("elements") && (*model)["elements"].is_array() &&
        !(*model)["elements"].empty()) {
      elements = &(*model)["elements"];
      break;
    }
  }
  if (!elements) {
    out.valid = true;
    out.full_cube = false;
    return true;
  }

  const Json* full = nullptr;
  for (const Json& element : *elements) {
    if (is_full_cube_element(element)) {
      full = &element;
      break;
    }
  }
  out.valid = true;
  if (!full) {
    out.full_cube = false;
    return true;
  }

  out.full_cube = true;
  const Json* faces = full->contains("faces") && (*full)["faces"].is_object()
                          ? &(*full)["faces"]
                          : nullptr;
  for (int i = 0; i < kFaceCount; ++i) {
    std::string texture_value;
    if (faces) {
      const auto face = faces->find(kFaceNames[i]);
      if (face != faces->end()) {
        if (face->is_string()) {
          texture_value = face->get<std::string>();
        } else if (face->is_object()) {
          if (face->contains("texture") && (*face)["texture"].is_string()) {
            texture_value = (*face)["texture"].get<std::string>();
          } else if (face->contains("sprite") && (*face)["sprite"].is_string()) {
            texture_value = (*face)["sprite"].get<std::string>();
          }
        }
      }
    }
    std::string sprite = resolve_texture(texture_value, textures, 0);
    if (sprite.empty() && !textures.empty()) {
      sprite = resolve_texture("#particle", textures, 0);
    }
    if (sprite.empty()) sprite = resolve_texture("#all", textures, 0);
    out.face_sprites[i] = sprite;
    out.face_tints[i] = default_tint(sprite);
    if (sprite.empty()) {
      out.warnings.push_back(std::string("no texture for face ") + kFaceNames[i]);
    }
  }
  return true;
}

} // namespace reim::conv
