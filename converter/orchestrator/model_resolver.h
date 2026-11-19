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

struct ResolvedModel {
  bool valid = false;
  bool full_cube = false;
  std::string model_ref;
  std::array<std::string, kFaceCount> face_sprites{};
  std::array<uint32_t, kFaceCount> face_tints{};
  std::vector<std::string> warnings;
};

class ModelResolver {
 public:
  ModelResolver(const zip::Archive& zip, std::vector<std::string>& warnings);

  bool resolve(const std::string& model_ref, ResolvedModel& out, std::string& error);

  static uint32_t default_tint(const std::string& sprite);

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

  const zip::Archive& zip_;
  std::vector<std::string>& warnings_;
  std::unordered_map<std::string, std::unique_ptr<ModelDoc>> cache_;
};

} // namespace reim::conv
