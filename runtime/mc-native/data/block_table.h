#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "json.hpp"

namespace reim::data {

using Json = nlohmann::json;

enum class RenderClass : uint8_t {
  skip = 0,       // air or an unsupported model; M1 does not mesh it
  opaque = 1,     // full cube or explicit geometry, writes depth (alpha-tested)
  transparent = 2 // full cube, blended (water, glass, ice)
};

/* Matches generated blocks.json tint_kinds; resolved per block position from
 * the section's biome palette (S4-5). */
enum class TintKind : uint8_t { none = 0, grass = 1, foliage = 2, water = 3 };

/* Face order matches generated/26.3/blocks.json: down, up, north, south,
 * west, east. north is -Z, south +Z, west -X, east +X. */
constexpr int kFaceCount = 6;
extern const char* const kFaceNames[kFaceCount];

struct FaceTexture {
  uint16_t sprite = 0;      // atlas index, informational (textures.json)
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  uint32_t tint = 0xFFFFFFFFu; // RGBA8, r in the low byte
  TintKind tint_kind = TintKind::none;
};

/* One explicit quad for non-cube models (snow layers, crosses, boxes). */
struct GeometryQuad {
  float corners[4][3]{}; // block-local 0..16
  float uv[4][2]{};      // atlas normalized
  float normal[3]{0.0f, 1.0f, 0.0f};
  uint16_t sprite = 0; // atlas index, informational
  uint32_t tint = 0xFFFFFFFFu;
  TintKind tint_kind = TintKind::none;
  int8_t cull_face = -1; // kFaceNames index, -1 = never cull
};

struct BlockDef {
  uint32_t id = 0;
  std::string name = "minecraft:air";
  bool full_block = false;
  bool collision = false;
  bool liquid = false;
  RenderClass render_class = RenderClass::skip;
  std::array<FaceTexture, kFaceCount> faces{};
  std::vector<GeometryQuad> geometry;
};

struct BiomeTint {
  uint32_t grass = 0xFFFFFFFFu;
  uint32_t foliage = 0xFFFFFFFFu;
  uint32_t water = 0xFFFFFFFFu;
};

/*
 * Converter output consumed by the Runtime (M1-E4, extended for S4):
 * blocks.json maps a block name (and optional property variants) to its render
 * classification, per-face sprite/tint and explicit geometry; textures.json
 * maps a sprite name to atlas UVs; biome_tints.json maps a biome to the grass,
 * foliage and water colours. Id 0 is always minecraft:air.
 *
 * The table is immutable after load and safe to read from the simulation
 * thread only (like the rest of the runtime data).
 */
class BlockTable {
 public:
  bool load(const std::filesystem::path& generated_dir, std::string& error);

  uint32_t find(std::string_view name) const;
  /* Resolves a palette state to its visual definition. Falls back to the
   * block's representative variant when no state-specific entry exists. */
  uint32_t find_state(std::string_view name,
                      const std::map<std::string, std::string, std::less<>>& properties) const;
  const BlockDef& def(uint32_t id) const;
  uint32_t size() const { return static_cast<uint32_t>(defs_.size()); }
  uint32_t air_id() const { return 0; }

  /* Biome colour for a tint kind; false (and no write) when unknown. */
  bool biome_color(std::string_view biome, TintKind kind, uint32_t& out) const;

 private:
  void parse_visual(BlockDef& def, const Json& entry,
                    const std::map<std::string, FaceTexture>& sprites);

  std::vector<BlockDef> defs_;
  std::unordered_map<std::string, uint32_t> by_name_;
  std::unordered_map<std::string, uint32_t> by_state_;
  std::unordered_map<std::string, BiomeTint> biomes_;
};

} // namespace reim::data
