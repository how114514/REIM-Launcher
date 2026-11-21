#pragma once

#include "common.h"
#include "util/zip.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace reim::conv {

constexpr int kFaceCount = 6;
extern const char* const kFaceNames[kFaceCount]; // down, up, north, south, west, east

/* How a face's final colour is derived (S4-5). `none` uses the stored tint,
 * the others look the biome colour up per block position at mesh time. */
enum class TintKind : uint8_t { none = 0, grass = 1, foliage = 2, water = 3 };

/* One face of a non-cube model element, in block-local 0..16 units. */
struct ResolvedQuad {
  std::array<std::array<float, 3>, 4> corners{};
  std::array<std::array<float, 2>, 4> uv{}; // 0..16 within the sprite
  std::array<float, 3> normal{{0.0f, 1.0f, 0.0f}};
  std::string sprite;
  TintKind tint = TintKind::none;
  int8_t cull_face = -1; // kFaceNames index, -1 = never cull
  bool double_sided = false;
};

struct ResolvedModel {
  bool valid = false;
  bool full_cube = false;
  bool has_geometry = false;
  std::string model_ref;
  std::array<std::string, kFaceCount> face_sprites{};
  std::array<uint32_t, kFaceCount> face_tints{};
  std::array<TintKind, kFaceCount> face_tint_kinds{};
  std::vector<ResolvedQuad> geometry;
  std::vector<std::string> warnings;
};

class ModelResolver {
 public:
  ModelResolver(const zip::Archive& zip, std::vector<std::string>& warnings);

  bool resolve(const std::string& model_ref, ResolvedModel& out, std::string& error);

  /* Fixed ARGB tint for sprites whose colour does not come from the biome
   * colormap (spruce/birch/cherry/azalea leaves). Returns white otherwise. */
  static uint32_t default_tint(const std::string& sprite);
  static TintKind tint_kind(const std::string& sprite);

 private:
  struct ModelDoc {
    Json json;
    bool valid = false;
    std::string error;
  };

  const ModelDoc* load_doc(const std::string& normalized);
  static std::string normalize_model(const std::string& model_ref);
  static std::string resolve_texture(const std::string& value,
                                     const std::unordered_map<std::string, std::string>& textures,
                                     int depth);
  static bool is_full_cube_element(const Json& element);

  bool resolve_geometry(const Json& elements,
                        const std::unordered_map<std::string, std::string>& textures,
                        ResolvedModel& out);

  const zip::Archive& zip_;
  std::vector<std::string>& warnings_;
  std::unordered_map<std::string, std::unique_ptr<ModelDoc>> cache_;
};

} // namespace reim::conv
