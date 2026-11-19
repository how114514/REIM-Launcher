#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reim::data {

enum class RenderClass : uint8_t {
  skip = 0,       // air or a non-cube model; M1 does not mesh it
  opaque = 1,     // full cube, writes depth
  transparent = 2 // full cube, blended (water, glass, ice)
};

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
};

struct BlockDef {
  uint32_t id = 0;
  std::string name = "minecraft:air";
  bool full_block = false;
  bool collision = false;
  bool liquid = false;
  RenderClass render_class = RenderClass::skip;
  std::array<FaceTexture, kFaceCount> faces{};
};

/*
 * Converter output consumed by the Runtime (M1-E4): blocks.json maps a block
 * name to its render classification and per-face sprite/tint, textures.json
 * maps a sprite name to atlas UVs. Id 0 is always minecraft:air.
 *
 * The table is immutable after load and safe to read from the simulation
 * thread only (like the rest of the runtime data).
 */
class BlockTable {
 public:
  bool load(const std::filesystem::path& generated_dir, std::string& error);

  uint32_t find(std::string_view name) const;
  const BlockDef& def(uint32_t id) const;
  uint32_t size() const { return static_cast<uint32_t>(defs_.size()); }
  uint32_t air_id() const { return 0; }

 private:
  std::vector<BlockDef> defs_;
  std::unordered_map<std::string, uint32_t> by_name_;
};

} // namespace reim::data
