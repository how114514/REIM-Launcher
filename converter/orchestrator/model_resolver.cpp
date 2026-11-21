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

int face_index(const std::string& name) {
  for (int i = 0; i < kFaceCount; ++i) {
    if (name == kFaceNames[i]) return i;
  }
  return -1;
}

void face_normal(int face, float out[3]) {
  static const float kNormals[kFaceCount][3] = {
      {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, -1.0f},
      {0.0f, 0.0f, 1.0f},  {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
  out[0] = kNormals[face][0];
  out[1] = kNormals[face][1];
  out[2] = kNormals[face][2];
}

/* Corner order matches the Runtime's full-cube faces (counter-clockwise seen
 * from outside), so the same index pattern works for both paths. */
void box_corners(int face, float x0, float y0, float z0, float x1, float y1,
                 float z1, float out[4][3]) {
  const float x[2] = {x0, x1};
  const float y[2] = {y0, y1};
  const float z[2] = {z0, z1};
  switch (face) {
    case 0: // down
      out[0][0] = x[0]; out[0][1] = y[0]; out[0][2] = z[0];
      out[1][0] = x[1]; out[1][1] = y[0]; out[1][2] = z[0];
      out[2][0] = x[1]; out[2][1] = y[0]; out[2][2] = z[1];
      out[3][0] = x[0]; out[3][1] = y[0]; out[3][2] = z[1];
      break;
    case 1: // up
      out[0][0] = x[0]; out[0][1] = y[1]; out[0][2] = z[0];
      out[1][0] = x[0]; out[1][1] = y[1]; out[1][2] = z[1];
      out[2][0] = x[1]; out[2][1] = y[1]; out[2][2] = z[1];
      out[3][0] = x[1]; out[3][1] = y[1]; out[3][2] = z[0];
      break;
    case 2: // north
      out[0][0] = x[0]; out[0][1] = y[0]; out[0][2] = z[0];
      out[1][0] = x[0]; out[1][1] = y[1]; out[1][2] = z[0];
      out[2][0] = x[1]; out[2][1] = y[1]; out[2][2] = z[0];
      out[3][0] = x[1]; out[3][1] = y[0]; out[3][2] = z[0];
      break;
    case 3: // south
      out[0][0] = x[0]; out[0][1] = y[0]; out[0][2] = z[1];
      out[1][0] = x[1]; out[1][1] = y[0]; out[1][2] = z[1];
      out[2][0] = x[1]; out[2][1] = y[1]; out[2][2] = z[1];
      out[3][0] = x[0]; out[3][1] = y[1]; out[3][2] = z[1];
      break;
    case 4: // west
      out[0][0] = x[0]; out[0][1] = y[0]; out[0][2] = z[0];
      out[1][0] = x[0]; out[1][1] = y[0]; out[1][2] = z[1];
      out[2][0] = x[0]; out[2][1] = y[1]; out[2][2] = z[1];
      out[3][0] = x[0]; out[3][1] = y[1]; out[3][2] = z[0];
      break;
    default: // east
      out[0][0] = x[1]; out[0][1] = y[0]; out[0][2] = z[0];
      out[1][0] = x[1]; out[1][1] = y[1]; out[1][2] = z[0];
      out[2][0] = x[1]; out[2][1] = y[1]; out[2][2] = z[1];
      out[3][0] = x[1]; out[3][1] = y[0]; out[3][2] = z[1];
      break;
  }
}

void face_uv_fraction(int face, float out[4][2]) {
  static const float kUv[kFaceCount][4][2] = {
      {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}, // down
      {{0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}, // up
      {{0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}, // north
      {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}, // south
      {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}, // west
      {{0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}, // east
  };
  for (int i = 0; i < 4; ++i) {
    out[i][0] = kUv[face][i][0];
    out[i][1] = kUv[face][i][1];
  }
}

const char* tint_kind_name(TintKind kind) {
  switch (kind) {
    case TintKind::grass: return "grass";
    case TintKind::foliage: return "foliage";
    case TintKind::water: return "water";
    default: return "";
  }
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
  // Some 26.3 models reference a texture alias without the '#' prefix.
  if (current.find('/') == std::string::npos &&
      current.find(':') == std::string::npos) {
    const auto alias = textures.find(current);
    if (alias != textures.end()) {
      return resolve_texture(alias->second, textures, depth + 1);
    }
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
  // Birch and spruce foliage use fixed colours in vanilla; other leaves and
  // grass follow the biome colormap and are tinted per block position.
  if (sprite == "block/birch_leaves") return 0xFF80A755u;
  if (sprite == "block/spruce_leaves") return 0xFF619961u;
  return 0xFFFFFFFFu;
}

TintKind ModelResolver::tint_kind(const std::string& sprite) {
  if (sprite == "block/grass_block_top" ||
      sprite == "block/grass_block_side_overlay" || sprite == "block/vine") {
    return TintKind::grass;
  }
  if (sprite == "block/water_still" || sprite == "block/water_flow") {
    return TintKind::water;
  }
  if (sprite.ends_with("_leaves")) {
    return default_tint(sprite) == 0xFFFFFFFFu ? TintKind::foliage
                                               : TintKind::none;
  }
  return TintKind::none;
}

bool ModelResolver::resolve_geometry(
    const Json& elements,
    const std::unordered_map<std::string, std::string>& textures,
    ResolvedModel& out) {
  // S4 covers crosses, thin blocks (snow layers) and simple boxes. Models with
  // many elements (stairs, fences, doors, redstone) stay on the skip path
  // until R1 rather than being approximated as oversized boxes.
  if (elements.size() > 2) return false;

  bool converted_any = false;
  for (const Json& element : elements) {
    if (!element.is_object()) continue;
    if (!element.contains("from") || !element.contains("to")) continue;
    float from[3];
    float to[3];
    if (!read_vec3(element["from"], from) || !read_vec3(element["to"], to)) continue;
    // Inverted helper elements (from > to) exist for under-face shading only.
    if (to[0] < from[0] || to[1] < from[1] || to[2] < from[2]) continue;

    const Json* faces = element.contains("faces") && element["faces"].is_object()
                            ? &element["faces"]
                            : nullptr;
    if (faces == nullptr || faces->empty()) continue;

    // Cross plants: a zero-thickness plane rotated 45 degrees about Y.
    const Json* rotation = element.contains("rotation") && element["rotation"].is_object()
                               ? &element["rotation"]
                               : nullptr;
    const bool y_rotation =
        rotation != nullptr && rotation->contains("axis") &&
        (*rotation)["axis"].is_string() &&
        (*rotation)["axis"].get<std::string>() == "y" &&
        rotation->contains("angle") && (*rotation)["angle"].is_number();
    const float angle = y_rotation ? (*rotation)["angle"].get<float>() : 0.0f;

    const bool thin_z = nearly(to[2] - from[2], 0.0f);
    const bool thin_x = nearly(to[0] - from[0], 0.0f);
    if (y_rotation && std::fabs(std::fabs(angle) - 45.0f) < 1e-3f &&
        (thin_z || thin_x)) {
      // Pick the sprite/tint from any face of the element.
      std::string sprite;
      TintKind tint = TintKind::none;
      float u0 = 0.0f, v0 = 0.0f, u1 = 16.0f, v1 = 16.0f;
      for (auto it = faces->begin(); it != faces->end(); ++it) {
        const Json& face = it.value();
        if (!face.is_object()) continue;
        std::string texture_value;
        if (face.contains("texture") && face["texture"].is_string()) {
          texture_value = face["texture"].get<std::string>();
        }
        sprite = resolve_texture(texture_value, textures, 0);
        if (face.contains("tintindex") && face["tintindex"].is_number()) {
          tint = TintKind::grass;
        }
        if (face.contains("uv") && face["uv"].is_array() && face["uv"].size() == 4) {
          u0 = face["uv"][0].get<float>();
          v0 = face["uv"][1].get<float>();
          u1 = face["uv"][2].get<float>();
          v1 = face["uv"][3].get<float>();
        }
        if (!sprite.empty()) break;
      }
      if (sprite.empty()) continue;

      auto emit_plane = [&](bool along_x) {
        ResolvedQuad front;
        front.sprite = sprite;
        front.tint = tint;
        front.normal = {0.0f, 1.0f, 0.0f}; // shade_direction_override: up
        front.cull_face = -1;
        float corners[4][3];
        if (along_x) {
          const float points[4][3] = {{0.0f, 0.0f, 0.0f}, {16.0f, 0.0f, 16.0f},
                                      {16.0f, 16.0f, 16.0f}, {0.0f, 16.0f, 0.0f}};
          for (int i = 0; i < 4; ++i) {
            for (int c = 0; c < 3; ++c) corners[i][c] = points[i][c];
          }
        } else {
          const float points[4][3] = {{16.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 16.0f},
                                      {0.0f, 16.0f, 16.0f}, {16.0f, 16.0f, 0.0f}};
          for (int i = 0; i < 4; ++i) {
            for (int c = 0; c < 3; ++c) corners[i][c] = points[i][c];
          }
        }
        const float uv_frac[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f},
                                     {1.0f, 0.0f}, {0.0f, 0.0f}};
        for (int i = 0; i < 4; ++i) {
          for (int c = 0; c < 3; ++c) front.corners[i][c] = corners[i][c];
          front.uv[i][0] = u0 + uv_frac[i][0] * (u1 - u0);
          front.uv[i][1] = v0 + uv_frac[i][1] * (v1 - v0);
        }
        out.geometry.push_back(front);

        // Back face (same plane, reversed winding and UV assignment).
        ResolvedQuad back = front;
        const int order[4] = {0, 3, 2, 1};
        const float back_uv[4][2] = {{0.0f, 1.0f}, {0.0f, 0.0f},
                                     {1.0f, 0.0f}, {1.0f, 1.0f}};
        for (int i = 0; i < 4; ++i) {
          for (int c = 0; c < 3; ++c) {
            back.corners[i][c] = corners[order[i]][c];
          }
          back.uv[i][0] = u0 + back_uv[i][0] * (u1 - u0);
          back.uv[i][1] = v0 + back_uv[i][1] * (v1 - v0);
        }
        out.geometry.push_back(back);
      };
      emit_plane(thin_z);
      converted_any = true;
      continue;
    }

    if (y_rotation && !nearly(angle, 0.0f)) {
      // Rotated boxes (fences, doors, torches, ...) are outside S4's geometry
      // scope; leave that block to the skip path instead of inventing geometry.
      continue;
    }

    // Zero-thickness planes (cross sprites) are handled above; any other flat
    // element would emit degenerate quads.
    if (nearly(to[0] - from[0], 0.0f) || nearly(to[1] - from[1], 0.0f) ||
        nearly(to[2] - from[2], 0.0f)) {
      continue;
    }

    for (auto it = faces->begin(); it != faces->end(); ++it) {
      const int face = face_index(it.key());
      if (face < 0) continue;
      const Json& face_json = it.value();
      if (!face_json.is_object()) continue;
      std::string texture_value;
      if (face_json.contains("texture") && face_json["texture"].is_string()) {
        texture_value = face_json["texture"].get<std::string>();
      } else if (face_json.contains("sprite") && face_json["sprite"].is_string()) {
        texture_value = face_json["sprite"].get<std::string>();
      }
      const std::string sprite = resolve_texture(texture_value, textures, 0);
      if (sprite.empty()) continue;

      float u0 = 0.0f, v0 = 0.0f, u1 = 16.0f, v1 = 16.0f;
      if (face_json.contains("uv") && face_json["uv"].is_array() &&
          face_json["uv"].size() == 4) {
        u0 = face_json["uv"][0].get<float>();
        v0 = face_json["uv"][1].get<float>();
        u1 = face_json["uv"][2].get<float>();
        v1 = face_json["uv"][3].get<float>();
      }

      ResolvedQuad quad;
      quad.sprite = sprite;
      quad.tint = face_json.contains("tintindex") ? TintKind::grass : TintKind::none;
      quad.cull_face = -1;
      if (face_json.contains("cullface") && face_json["cullface"].is_string()) {
        quad.cull_face = static_cast<int8_t>(
            face_index(face_json["cullface"].get<std::string>()));
      }
      face_normal(face, quad.normal.data());
      float corners[4][3];
      box_corners(face, from[0], from[1], from[2], to[0], to[1], to[2], corners);
      float uv_fraction[4][2];
      face_uv_fraction(face, uv_fraction);
      for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 3; ++c) quad.corners[i][c] = corners[i][c];
        quad.uv[i][0] = u0 + uv_fraction[i][0] * (u1 - u0);
        quad.uv[i][1] = v0 + uv_fraction[i][1] * (v1 - v0);
      }
      out.geometry.push_back(quad);
    }
    converted_any = true;
  }
  return converted_any;
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
    out.has_geometry = resolve_geometry(*elements, textures, out);
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
    out.face_tint_kinds[i] = tint_kind(sprite);
    if (sprite.empty()) {
      out.warnings.push_back(std::string("no texture for face ") + kFaceNames[i]);
    }
  }
  return true;
}

} // namespace reim::conv
